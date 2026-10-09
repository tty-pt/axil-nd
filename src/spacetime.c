#include "st.h"

#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pwd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <ttypt/corm.h>
#include <ttypt/qsys.h>
/* Host-side region API. NOT xy-mod.h: that header redefines xy_load and
 * friends as macros through a module-local `static struct xy_ctx xy`, and
 * this TU is engine code with no injected context -- including it here would
 * be the two-TU trap (each TU gets its own xy, only one ever injected).
 * The host globals below resolve to the axil process at dlopen time, the same
 * way axil_register already does from world.c and mods.c. */
#include <ttypt/xy.h>
#include <xxhash.h>

#include "config.h"
#include "noise.h"
#include "params.h"
#include "player.h"
#include "view.h"
#include "mcp.h"
#include "papi/nd.h"
#include "uapi/entity.h"

/* eng_map_where has no header declaration anywhere (spacetime.c:226 and
 * friends already call it unprototyped); say it out loud so the region anchor
 * below does not add another implicit declaration. */
void eng_map_where(pos_t p, unsigned thing);

#define PRECOVERY

#define GEON_RADIUS (VIEW_AROUND + 1)
#define GEON_SIZE (GEON_RADIUS * 2 + 1)
#define GEON_M (GEON_SIZE * GEON_SIZE)
#define GEON_BDI (GEON_SIZE * (GEON_SIZE - 1))

#define ROOM_COST 80

unsigned owner_hd = -1;

typedef void op_a_t(unsigned player_ref, enum exit e);
typedef int op_b_t(unsigned player_ref, struct cmd_dir cd);
typedef struct {
	union {
		op_a_t *a;
		op_b_t *b;
	} op;
	int type;
} op_t;
unsigned g_player_ref;
OBJ *g_player;
static unsigned *biome_map;

enum exit e_map[] = {
	[0 ... 254] = E_NULL,
	['h'] = E_WEST,
	['w'] = E_WEST,
	['j'] = E_SOUTH,
	['s'] = E_SOUTH,
	['k'] = E_NORTH,
	['n'] = E_NORTH,
	['l'] = E_EAST,
	['e'] = E_EAST,
	['K'] = E_UP,
	['u'] = E_UP,
	['J'] = E_DOWN,
	['d'] = E_DOWN,
};

exit_t exit_map[] = {
	[0 ... E_ALL] = {
		.simm = E_NULL,
		.name = "",
		.other = "",
		.dim = 5,
	},
	[E_EAST] = {
		.simm = E_WEST,
		.name = "east",
		.other = "wnsud",
		.dim = 1, .dis = 1,
	},
	[E_SOUTH] = {
		.simm = E_NORTH,
		.name = "south",
		.other = "ewudn",
		.dim = 0, .dis = 1,
	},
	[E_WEST] = {
		.simm = E_EAST,
		.name = "west",
		.other = "nsude",
		.dim = 1, .dis = -1,
	}, 
	[E_NORTH] = {
		.simm = E_SOUTH,
		.name = "north",
		.other = "sewud",
		.dim = 0, .dis = -1,
	},
	[E_UP] = {
		.simm = E_DOWN,
		.name = "up",
		.other = "dnsew",
		.dim = 2, .dis = 1,
	},
	[E_DOWN] = {
		.simm = E_UP,
		.name = "down",
		.other = "nsewu",
		.dim = 2, .dis = -1,
	},
};

unsigned long long day_tick = 0;
/* sub-tick remainder: nd_update() runs the world at 0.5 ticks per second, and
 * day_tick is an integer, so the halves have to be carried or day_tick would
 * truncate to 0 forever. Private, and not persisted. */
static double day_tick_frac = 0;
unsigned short day_n = 0;
double tick = 0;

static __inline__ ucoord_t
unsign(coord_t n)
{
	ucoord_t r = ((smorton_t) n + COORD_MAX);

	if (r == UCOORD_MAX)
		return 1;

	return r;
}


/* spread3(x):
 *   Take x ∈ [0..0xFFFF] and produce a 64-bit word where
 *   its bit-i goes to bit-(3*i) in the result.
 *
 * Part of a Morton-3D encode:  code = spread3(x)
 *                                  | spread3(y)<<1
 *                                  | spread3(z)<<2
 */
static inline uint64_t spread3(uint32_t x)
{
    uint64_t v = x & 0xFFFFu;  /* keep only low 16 bits */
    v = (v | (v << 32)) & 0x1F00000000FFFFULL; /* make room for high triples */
    v = (v | (v << 16)) & 0x1F0000FF0000FFULL; /* down to 8-bit chunks */
    v = (v | (v << 8)) & 0x100F00F00F00F00FULL; /* down to 4-bit groups */
    v = (v | (v << 4)) & 0x10C30C30C30C30C3ULL; /* down to 2-bit groups */
    v = (v | (v << 2)) & 0x1249249249249249ULL; /* final 3 bit interleave */
    return v;
}

static inline uint64_t morton3_pack_u16(
		uint16_t x,
		uint16_t y,
		uint16_t z,
		uint16_t world)
{
    return spread3(x)
         | (spread3(y) << 1)
         | (spread3(z) << 2)
         | ((uint64_t)world << 48);
}

morton_t
pos_morton(pos_t p)
{
	upoint3D_t up;
	up[0] = unsign(p[0]);
	up[1] = unsign(p[1]);
	up[2] = unsign(p[2]);
	return morton3_pack_u16(up[0], up[1], up[2], 0) | ((morton_t) p[3] << 48);
}

static inline coord_t
sign(ucoord_t n)
{
	return (smorton_t) n - COORD_MAX;
}

/* compact_axis(): collect one out of every 3 bits from 'code',
 * starting at 'shift' (0 = x, 1 = y, 2 = z).  Returns low-order
 * 21 bits containing that coordinate.                         */
static inline uint32_t compact_axis(uint64_t code, unsigned shift)
{
    code >>= shift; /* align the desired series to LSB */
    /* first keep only 1---1---1 pattern → mask 0x1249249249249… */
    code &= 0x1249249249249249ULL;

    /* Now collapse gaps:  3→2 → 2→1 → 1→0 */
    code = (code ^ (code >> 2))  & 0x10C30C30C30C30C3ULL;
    code = (code ^ (code >> 4))  & 0x100F00F00F00F00FULL;
    code = (code ^ (code >> 8))  & 0x1F0000FF0000FFULL;
    code = (code ^ (code >> 16)) & 0x1F00000000FFFFULL;
    code = (code ^ (code >> 32)) & 0x00000000001FFFFFULL;

    return (uint32_t) code; /* low 21 bits hold the axis value */
}

static inline void decode3(uint64_t code,
                           uint32_t *x, uint32_t *y, uint32_t *z)
{
    *x = compact_axis(code, 0);   /* bits 0,3,6,…   */
    *y = compact_axis(code, 1);   /* bits 1,4,7,…   */
    *z = compact_axis(code, 2);   /* bits 2,5,8,…   */
}

void
morton_pos(pos_t p, morton_t code)
{
	static const morton_t mask_off = 0x0000FFFFFFFFFFFFULL;
	uint32_t uup[3] = { 0, 0, 0 };
	decode3(code & mask_off, &uup[0], &uup[1], &uup[2]);
	p[0] = sign(uup[0]);
	p[1] = sign(uup[1]);
	p[2] = sign(uup[2]);
	p[3] = OBITS(code);
}

void
eng_object_drop(unsigned where_ref, unsigned skel_id)
{
	pos_t pos;
        register int i;
	unsigned drop_id;
	OBJ where;
	const void *__v, *kp, *vp;
	unsigned c = corm_iter(adrop_hd, &skel_id, CM_RANGE);

	corm_get_copy(obj_hd, &where_ref, &(where));
	eng_map_where(pos, where.location);

	while (corm_next(&kp, &vp, c)) {
		DROP drop;
		skel_id = *(const unsigned *)kp;
		drop_id = *(const unsigned *)vp;
		corm_get_copy(drop_hd, &drop_id, &(drop));
		if (random() < (RAND_MAX >> drop.y)) {
			uint32_t v2 = XXH32((const char *) pos, sizeof(pos_t), 3);
                        int yield = drop.yield,
                            yield_v = drop.yield_v;

                        if (!yield) {
				OBJ obj;
				unsigned obj_ref = eng_object_add(&obj, drop.skel, where_ref, v2, 0);
				corm_put(obj_hd, &obj_ref, &obj);
                                continue;
                        }

                        yield += random() & yield_v;

                        for (i = 0; i < yield; i++) {
				v2 = XXH32((const char *) pos, sizeof(pos_t), 4 + i);
				OBJ obj;
                                unsigned obj_ref = eng_object_add(&obj, drop.skel, where_ref, v2, 0);
				corm_put(obj_hd, &obj_ref, &obj);
			}
                }
	}
}

int
e_exit_can(OBJ *player, enum exit e) {
	return e_ground(player->location, e);
}

int
e_ground(unsigned room, enum exit e)
{
	pos_t pos;

	if (e & (E_UP | E_DOWN))
		return 0;

	eng_map_where(pos, room);
	return pos[2] == 0;
}

void
pos_move(pos_t d, pos_t o, enum exit e) {
	exit_t *ex = &exit_map[e];
	memcpy(d, o, sizeof(coord_t) * 4);
	d[ex->dim] += ex->dis;
}

enum exit
dir_e(const char dir) {
	return e_map[(int) dir];
}

char
e_dir(enum exit e) {
	return exit_map[e].name[0];
}

enum exit
e_simm(enum exit e) {
	return exit_map[e].simm;
}

char *
e_name(enum exit e) {
	return (char *) exit_map[e].name;
}

char *
e_other(enum exit e) {
	return (char *) exit_map[e].other;
}

morton_t
point_rel_idx(point_t p, point_t s, smorton_t w)
{
	smorton_t s0 = s[Y_COORD],
		s1 = s[X_COORD];
	if (s0 > p[Y_COORD])
		s0 -= UCOORD_MAX;
	if (s1 > p[X_COORD])
		s1 -= UCOORD_MAX;
	return (p[Y_COORD] - s0) * w + (p[X_COORD] - s1);
}

void
st_update(double dt)
{
	const char * msg = NULL;
	day_tick_frac += dt;
	unsigned long long whole = (unsigned long long) day_tick_frac;
	day_tick_frac -= (double) whole;
	day_tick += whole;
	if (day_tick > (1ULL << DAYTICK_Y))
		day_tick = 0;

	if (day_tick >= (1ULL << (DAYTICK_Y - 1))) {
		if (day_n)
			return;
		msg = "The sun sets.\n";
		day_n = 1;
	} else if (day_n) {
		msg = "The sun rises.\n";
		day_n = 0;
	} else
		return;

	unsigned c = corm_iter(obj_hd, NULL, 0);
	OBJ iobj;
	unsigned iobj_ref;
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		iobj_ref = *(const unsigned *)kp;
		iobj = *(const OBJ *)vp;

		if (iobj.type == TYPE_ENTITY) {
			view(iobj_ref);
			mcp_tod(iobj_ref, day_n);
			nd_writef(iobj_ref, msg);
		}
	}
}

static inline void
st_pos(pos_t p, unsigned loc, enum exit e)
{
	pos_t aux;
	eng_map_where(aux, loc);
	pos_move(p, aux, e);
}

static unsigned
st_there(unsigned where, enum exit e)
{
	pos_t pos;
	st_pos(pos, where, e);
	return eng_map_get(pos);
}

static inline int
fee_fail(unsigned player_ref, OBJ *player, char *desc, char *info, unsigned cost)
{
	if (player->value < cost) {
		nd_writef(player_ref, "You can't afford to %s. (%dp)\n", desc, cost);
		return 1;
	} else {
		player->value -= cost;
		nd_writef(player_ref, "%s (-%dp). %s\n",
			   desc, cost, info);
		return 0;
	}
}

static int
st_claim(unsigned player_ref, OBJ *room) {
	ROO *rroom = (ROO *) &room->data;

	if (!(rroom->flags & RF_TEMP)) {
		if (room->owner != player_ref) {
			nd_writef(player_ref, "You don't own this room\n");
			return 1;
		}

		return 0;
	}

	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	if (fee_fail(player_ref, &player, "claim a room", "", ROOM_COST))
		return 1;

	corm_put(obj_hd, &player_ref, &player);
	rroom->flags ^= RF_TEMP;
	room->owner = player_ref;

	return 0;
}

static inline void
exits_fix(unsigned there_ref, enum exit e)
{
	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;
	const char *s;

	for (s = e_other(e); *s; s++) {
		enum exit e2 = dir_e(*s);
		unsigned othere_ref = st_there(there_ref, e2);

		if (othere_ref == NOTHING)
			continue;

		OBJ othere;
		corm_get_copy(obj_hd, &othere_ref, &(othere));
		ROO *rothere = (ROO *) &othere.data;

		if (rothere->flags & RF_TEMP) {
			continue;
		}

		if (!(rthere->exits & e2)) {
			if (!(rothere->exits & e_simm(e2)))
				continue;

			rthere->exits |= e2;
			continue;
		}

		if (!(rothere->exits & e_simm(e2)))
			rthere->exits &= ~e2;
	}

	corm_put(obj_hd, &there_ref, &there);
}

static void
exits_infer(unsigned here_ref, ROO *rhere)
{
	const char *s = "wsnedu";

	for (; *s; s++) {
		enum exit e = dir_e(*s);
		unsigned there_ref = st_there(here_ref, e);

		if (there_ref == NOTHING) {
                        if (e != E_UP && e != E_DOWN)
				rhere->exits |= e;
			continue;
		}

		OBJ there;
		corm_get_copy(obj_hd, &there_ref, &(there));
		ROO *rthere = (ROO *) &there.data;

		if (rthere->exits & e_simm(e)) {
			rhere->exits |= e;
			rhere->doors |= rthere->doors & e_simm(e);
		}
	}
}

void map_put(pos_t p, unsigned thing, int flags);

static unsigned
st_room_at(unsigned player_ref, pos_t pos)
{
	struct bio bio = eng_noise_point(pos);
	OBJ there;
	biome_map = biome_map_get(* (uint64_t *) pos);
	unsigned there_ref = eng_object_add(&there, biome_map[bio.bio_idx], 0, (uint64_t) &bio, 0);
	ROO *rthere = (ROO *) &there.data;
	/* Carved rooms are permanent; clear RF_TEMP so eng_room_clean
	 * does not collect the room when empty (§27.3). */
	rthere->flags &= ~RF_TEMP;
	map_put(pos, there_ref, 1);
	exits_infer(there_ref, rthere);

	fprintf(stderr, "st_room_at %u\n", player_ref);

	if (pos[2] != 0) {
		corm_put(obj_hd, &there_ref, &there);
		return there_ref;
	}

	rthere->floor = bio.bio_idx;
	corm_put(obj_hd, &there_ref, &there);
	uint32_t v = XXH32((const char *) pos, sizeof(pos_t), 1);
	nd_evt_spawn(player_ref, there_ref, bio, v);
	return there_ref;
}

void
do_bio(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
	unsigned player_ref = eng_fd_player(fd);
	struct bio bio;
	pos_t pos;
	OBJ player;

	corm_get_copy(obj_hd, &player_ref, &(player));
	if (!eng_map_has(player.location)) {
		nd_writef(player_ref, "You are nowhere.\n");
		return;
	}
	eng_map_where(pos, player.location);
	bio = eng_noise_point(pos);
	biome_map = biome_map_get(*(uint64_t *) pos);
	SKEL biome;
	corm_get_copy(skel_hd, &biome_map[bio.bio_idx], &(biome));
	nd_writef(player_ref, "tmp %d rn %u bio %s(%d)\n",
		bio.tmp, bio.rn, biome.name, bio.bio_idx);
}

static unsigned
st_room(unsigned player_ref, unsigned location, enum exit e)
{
	pos_t pos;
	st_pos(pos, location, e);
	return st_room_at(player_ref, pos);
}

static unsigned
e_move(unsigned player_ref, enum exit e) {
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	OBJ loc;
	corm_get_copy(obj_hd, &player.location, &(loc));
	unsigned dest_ref;
	ROO *rloc = (ROO *) &loc.data;
	char const *dwts = "door";
	int door = 0;

	int cant_move = nd_evt_move(player_ref);
	if (cant_move)
		return NOTHING;

	if (!eng_map_has(player.location) || !(rloc->exits & e)) {
		nd_writef(player_ref, "You can't go that way.\n");
		return NOTHING;
	}

	if (rloc->doors & e) {
		if (e == E_UP || e == E_DOWN) {
			dwts = "hatch";
			door = 2;
		} else
			door = 1;

		nd_writef(player_ref, "You open the %s.\n", dwts);
	}

	dest_ref = st_there(player.location, e);
	if (dest_ref == NOTHING)
		dest_ref = st_room(player_ref, player.location, e);

	eng_enter(player_ref, dest_ref, e);

	if (door)
		nd_writef(player_ref, "You close the %s.\n", dwts);

	return dest_ref;
}

unsigned
eng_room_clean(unsigned here_ref)
{
	unsigned tmp_ref;
	OBJ here;

	corm_get_copy(obj_hd, &here_ref, &(here));

	if (!(((ROO *) here.data)->flags & RF_TEMP))
		return here_ref;

	unsigned c = corm_iter(contents_hd, &here_ref, CM_RANGE);
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		OBJ tmp;
		here_ref = *(const unsigned *)kp;
		tmp_ref = *(const unsigned *)vp;
		corm_get_copy(obj_hd, &tmp_ref, &(tmp));

		if (tmp.type != TYPE_ENTITY)
			continue;

		if (tmp.flags & OF_PLAYER) {
			corm_fin(c);
			return here_ref;
		}
	}

	eng_object_move(here_ref, NOTHING);
	return NOTHING;
}

static void
walk(unsigned player_ref, enum exit e) {
	e_move(player_ref, e);
}

static void
carve(unsigned player_ref, enum exit e)
{
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location, there_ref = here_ref;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;
	int wall = 0;

	if (!e_ground(here_ref, e)) {
		if (st_claim(player_ref, &here))
			return;

		rhere->exits |= e;
		corm_put(obj_hd, &here_ref, &here);

		there_ref = st_there(here_ref, e);
		if (there_ref == NOTHING)
			there_ref = st_room(player_ref, here_ref, e);
		wall = 1;
	}

	there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return;

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	st_claim(player_ref, &there); // FIXME check success in advance

	if (wall) {
		ROO *rthere = (ROO *) &there.data;
		const char *s;
		for (s = e_other(e_simm(e)); *s; s++) {
			enum exit e2 = dir_e(*s);

			if ((rthere->exits & e2) &&
					st_there(there_ref, e2) != NOTHING)

				rthere->exits ^= e2;
		}
	}

	corm_put(obj_hd, &there_ref, &there);
}

static void
uncarve(unsigned player_ref, enum exit e)
{
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	const char *s0 = "is";
	unsigned here_ref = player.location, there_ref;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;
	int ht, cd = e_ground(here_ref, e);

	if (cd) {
		ht = rhere->flags & RF_TEMP;
		there_ref = e_move(player_ref, e);
		if (there_ref == NOTHING)
			return;
	} else {
		if (!(rhere->exits & e)) {
                        nd_writef(player_ref, "No exit there.\n");
                        return;
                }

		there_ref = st_there(here_ref, e);

		if (there_ref == NOTHING) {
			nd_writef(player_ref, "No room there.\n");
			return;
		}
		s0 = "at";
	}

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	if ((rthere->flags & RF_TEMP) || there.owner != player_ref) {
		nd_writef(player_ref, "You don't own th%s room.\n", s0);
		return;
	}

	rthere->flags ^= RF_TEMP;
	exits_infer(there_ref, rthere);
	if (cd) {
		if (ht && (rthere->doors & e_simm(e)))
			rthere->doors &= ~e_simm(e);
		corm_put(obj_hd, &there_ref, &there);
	} else
		eng_room_clean(there_ref);

	corm_get_copy(obj_hd, &player_ref, &(player));
	player.value += ROOM_COST;
	nd_writef(player_ref, "You collect your materials. (+%dp)\n", ROOM_COST);
	corm_put(obj_hd, &player_ref, &player);
}

static void
unwall(unsigned player_ref, enum exit e)
{
	int a, b, c, d;
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned there_ref, here_ref = player.location;
	OBJ here;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	a = here.owner == player_ref;
	b = rhere->flags & RF_TEMP;
	there_ref = st_there(here_ref, e);

	OBJ there;
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	if (there_ref != NOTHING) {
		c = there.owner == player_ref;
		d = rthere->flags & RF_TEMP;
	} else {
		c = 0;
		d = 1;
	}

	if (!((a && !b && (d || c))
	    || (c && !d && b))) {
		nd_writef(player_ref, "You can't do that here.\n");
		return;
	}

	if (rhere->exits & e) {
		nd_writef(player_ref, "There's an exit here already.\n");
		return;
	}

	rhere->exits |= e;
	there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return;

	corm_get_copy(obj_hd, &there_ref, &(there));
	rthere = (ROO *) &there.data;
	rthere->exits |= e_simm(e);
	corm_put(obj_hd, &there_ref, &there);
	nd_writef(player_ref, "You tear down the wall.\n");
}

static inline int
gexit_claim(unsigned player_ref, enum exit e)
{
	int a, b, c, d;
	OBJ player, here, there;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location,
	      there_ref = st_there(player.location, e);
	corm_get_copy(obj_hd, &here_ref, &(here));
	corm_get_copy(obj_hd, &there_ref, &(there));
	ROO *rthere = (ROO *) &there.data;

	a = here_ref != NOTHING && here.owner == player_ref;
	c = rthere->flags & RF_TEMP;
	b = !c && there.owner == player_ref;
	d = e_ground(here_ref, e);

	if (a && (b || c))
		return 0;

	if (here_ref != NOTHING || (((ROO *) here.data)->flags & RF_TEMP)) {
		if (b)
			return 0;
		if (d || c) // FIXME
			return st_claim(player_ref, &there);
	}

	nd_writef(player_ref, "You can't claim that exit.\n");
	return 1;
}

static inline int
gexit_claim_walk(unsigned player_ref, enum exit e)
{
	OBJ player, here;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	if (!(rhere->exits & e)) {
		nd_writef(player_ref, "No exit here.\n");
		return 1;
	}

	unsigned there_ref = e_move(player_ref, e);
	if (there_ref == NOTHING)
		return 1;

	player.location = there_ref;
	corm_put(obj_hd, &player_ref, &player);

	return gexit_claim(player_ref, e_simm(e));
}

static void
e_wall(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, here;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location;
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	rhere->exits &= ~e_simm(e);
	corm_put(obj_hd, &here_ref, &here);

	unsigned there_ref = st_there(here_ref, e_simm(e));

	if (there_ref != NOTHING) {
		OBJ there;
		corm_get_copy(obj_hd, &there_ref, &(there));
		ROO *rthere = (ROO *) &there.data;
		rthere->exits &= ~e;
		corm_put(obj_hd, &there_ref, &there);
	}

	nd_writef(player_ref, "You build a wall.\n");
}

static void
door(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, where;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned where_ref = player.location;
	corm_get_copy(obj_hd, &where_ref, &(where));
	ROO *rwhere = (ROO *) &where.data;
	rwhere->doors |= e_simm(e);
	corm_put(obj_hd, &where_ref, &where);

	where_ref = st_there(where_ref, e_simm(e));

	if (where_ref == NOTHING) {
		corm_get_copy(obj_hd, &where_ref, &(where));
		rwhere = (ROO *) &where.data;
		rwhere->doors |= e;
		corm_put(obj_hd, &where_ref, &where);
	}

	nd_writef(player_ref, "You place a door.\n");
}

static void
undoor(unsigned player_ref, enum exit e)
{
	if (gexit_claim_walk(player_ref, e))
		return;

	OBJ player, where;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned where_ref = player.location;
	corm_get_copy(obj_hd, &where_ref, &(where));
	ROO *rwhere = (ROO *) &where.data;
	rwhere->doors &= ~e_simm(e);
	corm_put(obj_hd, &where_ref, &where);

	where_ref = st_there(where_ref, e_simm(e));

	if (where_ref != NOTHING) {
		corm_get_copy(obj_hd, &where_ref, &(where));
		rwhere = (ROO *) &where.data;
		rwhere->doors &= ~e;
		corm_put(obj_hd, &where_ref, &where);
	}

	nd_writef(player_ref, "You remove a door.\n");
}

static int
tell_pos(unsigned player_ref, struct cmd_dir cd) {
	pos_t pos;
	unsigned target_ref = cd.rep == 1 ? player_ref : cd.rep;
	OBJ target;
	corm_get_copy(obj_hd, &target_ref, &(target));
	int ret = 1;

	if (target.type != TYPE_ENTITY) {
		nd_writef(player_ref, "Invalid object type.\n");
		return 0;
	}

	eng_map_where(pos, target.location);
	nd_writef(player_ref, "0x%llx\n", MORTON_READ(pos));
	return ret;
}

void
eng_st_teleport(unsigned player_ref, uint64_t mpos) {
	pos_t pos;
	memcpy(pos, &mpos, sizeof(mpos));
	unsigned there_ref = eng_map_get(pos);
	biome_map = biome_map_get(* (uint16_t *) pos);
	if (there_ref == NOTHING)
		there_ref = st_room_at(player_ref, pos);
	eng_enter(player_ref, there_ref, E_NULL);
}

int
vim_teleport(unsigned player_ref, struct cmd_dir cd)
{
	pos_t pos;
	int ret = 0;
	if (cd.rep == 1)
		cd.rep = 0;
	if (cd.dir == '?') {
		// X6? teleport to chivas
		unsigned target_ref = cd.rep;
		OBJ target;
		corm_get_copy(obj_hd, &target_ref, &(target));
		if (target.type == TYPE_ENTITY) {
			eng_map_where(pos, target.location);
			ret = 1;
		}
		memcpy(&cd.rep, pos, sizeof(cd.rep));
	}

	eng_st_teleport(player_ref, cd.rep);
	return ret;
}

static int
pull(unsigned player_ref, struct cmd_dir cd)
{
	pos_t pos;
	enum exit e = cd.e;
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned here_ref = player.location, there_ref;

	if (e == E_NULL)
		return 0;

	unsigned what_ref = cd.rep;

	if (what_ref == NOTHING) {
		nd_writef(player_ref, "You cannot do that.\n");
		return 1;
	}

	OBJ what, here;
	corm_get_copy(obj_hd, &what_ref, &(what));
	corm_get_copy(obj_hd, &here_ref, &(here));
	ROO *rhere = (ROO *) &here.data;

	if (!(rhere->exits & e)
	    || what.type != TYPE_ROOM
	    || what.owner != player_ref
	    || ((there_ref = st_there(here_ref, e))
		&& eng_room_clean(there_ref) == there_ref))
	{
		nd_writef(player_ref, "You cannot do that.\n");
		return 1;
	}

	st_pos(pos, player.location, e);
	map_put(pos, cd.rep, 0);
	exits_fix(cd.rep, e);
	e_move(player_ref, e);
	return 1;
}

op_t op_map[256] = {
	['r'] = { .op.a = &carve },
	['R'] = { .op.a = &uncarve },
	['d'] = { .op.a = &door } ,
	['D'] = { .op.a = &undoor },
	['w'] = { .op.a = &e_wall },
	['W'] = { .op.a = &unwall },
	['x'] = { .op.b = &tell_pos, .type = 1 },
	['X'] = { .op.b = &vim_teleport, .type = 1 },
	['#'] = { .op.b = &pull, .type = 1 },
};

static int
st_cmd_dir(struct cmd_dir *res, const char *cmd)
{
	int ofs = 0;
	morton_t rep = 1;
	char *end, sc;

	if (isdigit(cmd[0])) {
		rep = strtoull(cmd, &end, 0);
		ofs += end - &cmd[0];
	}

	res->dir = sc = cmd[ofs];
	res->e = dir_e(sc);
	res->rep = rep;

	return ofs;
}

static void may_look(unsigned player_ref, morton_t old_loc) {
	OBJ player;
	morton_t new_loc;

	corm_get_copy(obj_hd, &player_ref, &(player));
	new_loc = eng_map_mwhere(player.location);

	if (old_loc == new_loc)
		return;

	eng_look_at(player_ref, NOTHING);
	view(player_ref);
	eng_nd_flush(player_ref);
}

int
st_v(unsigned player_ref, char const *opcs)
{
	OBJ player;
	if (!player_ref || !corm_get(obj_hd, &player_ref))
		return 0;
	corm_get_copy(obj_hd, &player_ref, &(player));
	morton_t old_loc = eng_map_mwhere(player.location);
	biome_map = biome_map_get(old_loc);
	if (!biome_map)
		return 0;
	struct cmd_dir cd;
	char const *s = opcs;

	for (;*s;) {
		int ofs = 0;
		op_t op = op_map[(unsigned char) *s]; // the op
		op_a_t *aop = op.type ? NULL : op.op.a; 
		ofs += !!(aop || op.type);
		ofs += st_cmd_dir(&cd, s + ofs);

		if (!(aop || op.op.b))
			aop = &walk;

		if (aop) {
			if (cd.e == E_NULL) {
				may_look(player_ref, old_loc);
				return s > opcs ? (int)(s - opcs) : 1;
			}

			ofs ++;
			if (cd.rep > 1000)
				cd.rep = 1000;
			morton_t j;
			for (j = 0; j < cd.rep; j++)
				aop(player_ref, cd.e);
		} else if (op.op.b)
			ofs += op.op.b(player_ref, cd);

		s += ofs;
	}

	may_look(player_ref, old_loc);
	return (int)(s - opcs);
}

void
echo(char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	nd_dwritef(g_player_ref, fmt, va);
	va_end(va);
}

void
oecho(char *format, ...) {
	va_list args;
	va_start(args, format);
	nd_owritef(g_player_ref, format, args);
	va_end(args);
}

/* ---------------------------------------------------------------------------
 * Region rows (ST.md §7.2 as amended by §22.1).
 *
 * One corm record per region, keyed "<16 hex id><2 hex plen>". The record
 * layout is registered once from nd_world_init before the `st` table opens;
 * every function below degrades to a miss when owner_hd is CM_MISS rather
 * than indexing corm_heads[CM_MISS], which would be an out-of-bounds read.
 * --------------------------------------------------------------------------- */

static int
st_have_hd(void)
{
	return owner_hd != CM_MISS;
}

uint32_t
st_rec_register(void)
{
	static int done = 0;
	static uint32_t rec = CM_MISS;

	if (done)
		return rec;
	done = 1;

	rec = corm_record_register("st_rec", sizeof(struct st_rec),
		(corm_record_field_t[]){
			{ .name = "owner", .type = CM_U32,
			  .offset = offsetof(struct st_rec, owner),
			  .max_size = sizeof(uint32_t) },
			{ .name = "plen", .type = CM_U32,
			  .offset = offsetof(struct st_rec, plen),
			  .max_size = sizeof(uint32_t) },
			{ .name = "nmods", .type = CM_U32,
			  .offset = offsetof(struct st_rec, nmods),
			  .max_size = sizeof(uint32_t) },
			{ .name = "flags", .type = CM_U32,
			  .offset = offsetof(struct st_rec, flags),
			  .max_size = sizeof(uint32_t) },
			{ .name = "mods", .type = CM_STR,
			  .offset = offsetof(struct st_rec, mods),
			  .max_size = sizeof(((struct st_rec *)0)->mods) },
		}, 5);
	if (rec == CM_MISS)
		WARN("st_rec_register: corm_record_register failed\n");
	return rec;
}

/* One-field owner read. Up to 65 regions can cover one position and each
 * probe must not copy a whole row (§22.1). */
unsigned
st_owner(uint64_t id, uint8_t plen)
{
	char key[ST_ROW_KEY_LEN + 1];
	char q[ST_ROW_KEY_LEN + 1 + sizeof(":owner")];
	const uint32_t *o;

	if (!st_have_hd())
		return NOTHING;
	st_row_key(key, sizeof(key), id, plen);
	snprintf(q, sizeof(q), "%s:owner", key);
	o = corm_get(owner_hd, q);
	if (!o)
		return NOTHING;
	return *o;
}

int
st_can(unsigned ref, uint64_t id, uint8_t plen)
{
	return st_owner(id, plen) == ref;
}

int
st_row_get(uint64_t id, uint8_t plen, struct st_rec *out)
{
	char key[ST_ROW_KEY_LEN + 1];
	const struct st_rec *r;

	if (!st_have_hd())
		return 0;
	st_row_key(key, sizeof(key), id, plen);
	r = corm_get(owner_hd, key);
	if (!r)
		return 0;
	if (out)
		memcpy(out, r, sizeof(*out));
	return 1;
}

void
st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec)
{
	char key[ST_ROW_KEY_LEN + 1];

	if (!st_have_hd() || !rec)
		return;
	st_row_key(key, sizeof(key), id, plen);
	corm_put(owner_hd, key, rec);
}

void
st_row_del(uint64_t id, uint8_t plen)
{
	char key[ST_ROW_KEY_LEN + 1];

	if (!st_have_hd())
		return;
	st_row_key(key, sizeof(key), id, plen);
	corm_del(owner_hd, key);
}

/* ---------------------------------------------------------------------------
 * Region claim + module load/unload (ST.md §22.3).
 * --------------------------------------------------------------------------- */

/* xy_with_region takes only (fn, ud), so the module stem rides in ud (§15).
 * xy_load takes a mutable char *, so the trampoline's ud must point at a
 * writable buffer -- every caller below passes a stack copy, never a literal. */
static int
st_load_tramp(void *ud)
{
	return xy_load((char *)ud);
}

static int
st_unload_tramp(void *ud)
{
	return xy_unload((char *)ud);
}

int
st_region_load(uint64_t id, uint8_t plen, char *name)
{
	int rc;

	if (!name || !*name || strchr(name, '/'))
		return XY_ERR_INVALID;
	/* Claim first: it is idempotent, and on success the region is current,
	 * so even a bare xy_load would land in it. The with_region wrapper is
	 * the belt to that suspender -- an explicit address, not a reliance on
	 * retained currentness. */
	rc = xy_claim_at(id, plen, NULL, NULL);
	if (rc != XY_OK)
		return rc;
	return xy_with_region(id, plen, st_load_tramp, name);
}

int
st_region_unload(uint64_t id, uint8_t plen, char *name)
{
	if (!name || !*name || strchr(name, '/'))
		return XY_ERR_INVALID;
	/* No claim here: the region must already exist, and with_region reports
	 * XY_ERR_NOTFOUND when it does not. */
	return xy_with_region(id, plen, st_unload_tramp, name);
}

int
st_mod_loaded(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;
	uint32_t i;

	if (!name || !st_row_get(id, plen, &rec))
		return 0;
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		if (strncmp(rec.mods[i], name, ND_ST_MOD_NAME) == 0)
			return 1;
	}
	return 0;
}

/* st_mod_add records the stem; it does NOT load. The caller loads first, so a
 * failure leaves the row exactly as it was -- a typo must not persist forever
 * and make every boot log the same failure (§22.5, §7.6). */
int
st_mod_add(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;

	if (!name || !*name || strchr(name, '/') ||
	    strlen(name) >= ND_ST_MOD_NAME)
		return XY_ERR_INVALID;
	if (!st_row_get(id, plen, &rec))
		return XY_ERR_NOTFOUND;
	if (st_mod_loaded(id, plen, name))
		return XY_OK;
	if (rec.nmods >= ND_ST_MAX_MODS)
		return XY_ERR_TOOBIG;
	memset(rec.mods[rec.nmods], 0, ND_ST_MOD_NAME);
	snprintf(rec.mods[rec.nmods], ND_ST_MOD_NAME, "%s", name);
	rec.nmods++;
	st_row_put(id, plen, &rec);
	return XY_OK;
}

int
st_mod_del(uint64_t id, uint8_t plen, const char *name)
{
	struct st_rec rec;
	uint32_t i, j;

	if (!name || !st_row_get(id, plen, &rec))
		return XY_ERR_NOTFOUND;
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		if (strncmp(rec.mods[i], name, ND_ST_MOD_NAME) == 0)
			break;
	}
	if (i >= rec.nmods || i >= ND_ST_MAX_MODS)
		return XY_ERR_NOTFOUND;
	for (j = i; j + 1 < rec.nmods && j + 1 < ND_ST_MAX_MODS; j++)
		memcpy(rec.mods[j], rec.mods[j + 1], ND_ST_MOD_NAME);
	rec.nmods--;
	memset(rec.mods[rec.nmods], 0, ND_ST_MOD_NAME);
	st_row_put(id, plen, &rec);
	return XY_OK;
}

/* The caller's position-derived region (§7.3 as amended by §22.4): the room's
 * pos_t through pos_morton, then the deepest covering region. Deliberately
 * not eng_map_mwhere, which reinterprets the 8 pos_t bytes as a word. */
int
st_region_of_player(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	OBJ player;
	pos_t pos;
	uint64_t code, rid;
	uint8_t w;

	if (!id || !plen)
		return XY_ERR_INVALID;
	if (player_ref == NOTHING || player_ref == (unsigned)-1)
		return XY_ERR_INVALID;
	memset(&player, 0, sizeof(player));
	corm_get_copy(obj_hd, &player_ref, &player);
	eng_map_where(pos, player.location);
	code = pos_morton(pos);
	rid = xy_region_at(code, ST_PLEN_CELL, &w);
	if (rid == XY_REGION_INVALID)
		return XY_ERR_NOTFOUND;
	*id = rid;
	*plen = w;
	return XY_OK;
}

/* Boot restore (ST.md §7.6, §22.2): snapshot every row, sort by plen
 * ascending, then claim and load. The sort is load-bearing, not tidy:
 * corm_iter order is unspecified, and xy_claim_at attaches to the nearest
 * EXISTING ancestor, so a child restored before its parent would land under
 * the wrong one. A failed load never clears the row -- a missing binary is
 * not a reason to forget the intent (§7.6) -- so a later boot retries it. */
struct st_restore_row {
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;
};

static int
st_restore_cmp(const void *a, const void *b)
{
	const struct st_restore_row *ra = a, *rb = b;

	if (ra->rec.plen < rb->rec.plen)
		return -1;
	if (ra->rec.plen > rb->rec.plen)
		return 1;
	if (ra->id < rb->id)
		return -1;
	if (ra->id > rb->id)
		return 1;
	return 0;
}

void
st_init(void) {
	unsigned c = corm_iter(owner_hd, NULL, 0);
	const void *kp, *vp;
	struct st_restore_row *rows = NULL;
	size_t n = 0, cap = 0;

	if (!st_have_hd())
		return;

	while (corm_next(&kp, &vp, c)) {
		const char *key = kp;
		const struct st_rec *rec = vp;
		uint64_t id;
		uint8_t plen;

		/* Anything that is not an 18-char row key is a legacy binary
		 * st_key from a pre-record store: refused, never parsed (§22.1)
		 * and left in place for the operator to drop, not reinterpreted. */
		if (st_row_key_parse(key, &id, &plen) != 0) {
			WARN("st_restore: refusing unparseable row key\n");
			continue;
		}
		if (rec->plen != plen) {
			WARN("st_restore: plen mismatch, refusing row\n");
			continue;
		}
		if (n >= cap) {
			size_t ncap = cap ? cap * 2 : 8;
			struct st_restore_row *nrows =
				realloc(rows, ncap * sizeof(*nrows));
			if (!nrows) {
				WARN("st_restore: out of memory, stopping\n");
				break;
			}
			rows = nrows;
			cap = ncap;
		}
		rows[n].id = id;
		rows[n].plen = plen;
		memcpy(&rows[n].rec, rec, sizeof(rows[n].rec));
		n++;
	}
	corm_fin(c);

	qsort(rows, n, sizeof(*rows), st_restore_cmp);

	for (size_t i = 0; i < n; i++) {
		int rc;
		uint32_t m;
		char namebuf[ND_ST_MOD_NAME + 1];

		rc = xy_claim_at(rows[i].id, rows[i].plen, NULL, NULL);
		if (rc != XY_OK) {
			WARN("st_restore: claim failed for region\n");
			continue;
		}
		/* WARN, not syslog: the suite greps the axil stderr capture for
		 * these lines, and syslog goes to /dev/log (§22.6). */
		WARN("st_restore: region id=0x%016llx plen=%u\n",
			(unsigned long long)rows[i].id, rows[i].plen);

		for (m = 0; m < rows[i].rec.nmods && m < ND_ST_MAX_MODS; m++) {
			memcpy(namebuf, rows[i].rec.mods[m], ND_ST_MOD_NAME);
			namebuf[ND_ST_MOD_NAME] = '\0';
			if (!namebuf[0])
				continue;
			rc = st_region_load(rows[i].id, rows[i].plen, namebuf);
			if (rc != XY_OK) {
				WARN("st_restore: module %s failed to load, keeping row\n",
					namebuf);
				continue;
			}
			WARN("st_restore: loaded %s\n", namebuf);
		}
	}
	free(rows);

	/* Restore root currentness. Every xy_claim_at() above makes its region
	 * current, and nothing puts it back: xy_with_region() only restores
	 * what was current when it was entered (the just-claimed planet), and
	 * the XY hook dispatch only restores the caller's region when the
	 * callee lives in a different one -- the engine lives at root, so a
	 * hook that calls this leaks the last planet as the process-wide
	 * current region. xy_call() dispatches to the current region's subtree,
	 * so from a planet every root module (engine, axil-tty, axil-auth)
	 * goes invisible and input lines vanish with XY_ERR_NOTFOUND. The old
	 * xy_install() containment (mod_load_enter/restore_context) did this
	 * implicitly; any caller outside it needs it done here. Root re-claim
	 * is explicitly clean (it is its own best match). */
	xy_claim_at(0, 0, NULL, NULL);
}

/* ---------------------------------------------------------------------------
 * Planet commands (ST.md §7.5 as amended by §22.5).
 *
 * `teleport` resolves a named object, never a world number, so there is no
 * in-game way to stand in world N and no honest way to derive "the caller's
 * current region" for a moderator who is elsewhere. The region-targeting
 * commands therefore take an explicit world: `loadmod <name> [world]`,
 * `modlist [world]`. Without one, the caller's position-derived region is
 * used. `here` is always position-derived.
 *
 * The gate on every mutating command is region ownership, never the module
 * list itself: the list is the enabled set, and loadmod is
 * the ruler's act of adding to it, so gating loadmod on membership would
 * deadlock an empty planet (§22.5). The code gate an outer ruler imposes is
 * xy_deny, enforced inside libxylem at load time.
 *
 * Every return path below flushes: eng_nd_write is a history+dedup buffer,
 * not a socket write, and nd_command only resets the dedup counter -- without
 * an explicit eng_nd_flush the reply sits in ioc[fd].buf until some later,
 * different write displaces it, which presents as a command that runs (the
 * row is written, the log shows it) but never answers (§5.3).
 * --------------------------------------------------------------------------- */

/* The one authorization rule for the region-tree primitives (`room`, `deny`):
 * you may act in a region you rule, and the cosmos ruler may act in ANY region.
 *
 * The cosmos clause is what makes the Phase 3 gate's bootstrap work at all.
 * §27.3's `room 0 0 0 1` necessarily runs BEFORE `planet 1`, so at that moment
 * world 1 has no row and no owner -- there is nobody whose ownership could
 * authorize it. The seeded cosmos row (world.c, owner = 1, the first player) is
 * the actor that can carve the first room out of a fresh world, exactly as
 * do_planet already requires cosmos ownership for a new planet claim.
 *
 * There is no wizard override here, and there never was one in practice: the
 * EF_WIZARD clause that used to sit at the top was unreachable (nothing in the
 * port ever set the flag), so deleting it left this function's reach
 * bit-identical. ST.md §27.6(1), NO_WIZ.md §3.
 *
 * Keep the reach narrow on purpose. The three loadmod/unloadmod/release callers
 * below gate on st_can() exactly, NOT on this function -- widening them to the
 * cosmos ruler would hand one player authority over every planet's module set,
 * which no decision asked for.
 */
/* "May this actor act anywhere in (id,plen)?"
 *
 * One loop over every ancestor width instead of "exact owner, else cosmos",
 * because the decision says a ruler governs what is UNDER them, not only what
 * they are. Masking (id,plen) down to width w yields the ancestor region id at
 * that width, so the candidate set is exactly {ancestors of (id,plen)}, and the
 * two cases the old shape special-cased are just the two ends of it: w == plen
 * is exact ownership, w == 0 is the cosmos, whose row covers the whole address
 * space.
 *
 * Widths 32 and 48 cannot hold a row today, so they are two wasted corm probes
 * per call. They are here rather than omitted because they are the difference
 * between "ancestor ownership" as a stated rule and as an accident of the
 * current tree depth: the day a row lands at 32, this keeps working with no
 * edit, and the omission would be a silent behaviour change nobody would catch.
 *
 * w > plen is skipped because a region cannot be its own ancestor's ancestor:
 * narrowing to a width the query does not span would invent a region that
 * does not contain the thing being asked about. */
int
st_can_region(unsigned player_ref, uint64_t id, uint8_t plen)
{
	static const uint8_t widths[] = {
		ST_PLEN_ROOT, ST_PLEN_WORLD, 32, 48, ST_PLEN_CELL
	};
	uint64_t mask;
	size_t i;

	/* A prefix width above 64 is not a region. ST_SEL_UNSET is 255, so a
	 * caller that forgot to resolve "unspecified" lands here rather than
	 * masking by a nonsense width. */
	if (plen > ST_PLEN_CELL)
		return 0;

	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		uint8_t w = widths[i];

		if (w > plen)
			continue;
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		if (st_can(player_ref, id & mask, w))
			return 1;
	}
	return 0;
}

/* Who governs (id,plen)? The same ancestor walk as st_can_region, but it
 * answers with the owner instead of a yes/no, and it stops at the FIRST
 * ancestor that has a row -- deepest wins, so a cell inside a ruled planet
 * names the planet's ruler and not the cosmos ruler who also, technically,
 * covers it.
 *
 * This exists because "you may not" is not an answer a player can act on: the
 * whole point of naming the ruler on a refusal (the do_room template) is to
 * tell them who to ask. Returning the shallowest owner would name the cosmos
 * ruler for every refusal on the planet, which is true and useless.
 *
 * Returns NOTHING when no ancestor has a row, i.e. the region is unclaimed --
 * distinct from "claimed by ref 0". st_can_region returning 0 with
 * st_region_ruler returning NOTHING means nobody rules here; with a real owner
 * it means somebody else does, which is the case a refusal must name. */
unsigned
st_region_ruler(uint64_t id, uint8_t plen)
{
	static const uint8_t widths[] = {
		ST_PLEN_ROOT, ST_PLEN_WORLD, 32, 48, ST_PLEN_CELL
	};
	/* Same widths as st_can_region, but walked deepest-first: the array is
	 * ascending, so it is walked in reverse. Reversing one array and not the
	 * other is the kind of thing that silently answers the wrong question, so
	 * both are derived from this one list below rather than each naming their
	 * own. */
	uint64_t mask;
	size_t i;

	if (plen > ST_PLEN_CELL)
		return NOTHING;

	for (i = sizeof(widths) / sizeof(widths[0]); i-- > 0; ) {
		uint8_t w = widths[i];

		if (w > plen)
			continue;
		mask = w ? (~0ULL << (64 - w)) : 0ULL;
		unsigned owner = st_owner(id & mask, w);

		if (owner != NOTHING)
			return owner;
	}
	return NOTHING;
}

/* The region a pos_t falls in: the same derivation as st_region_of_player
 * (pos_t through pos_morton, then the deepest covering region), for a position
 * that is not necessarily where anybody stands. */
static int
st_region_of_pos(coord_t *pos, uint64_t *id, uint8_t *plen)
{
	uint64_t code, rid;
	uint8_t w;

	if (!pos || !id || !plen)
		return XY_ERR_INVALID;
	/* `coord_t *pos`, not `const pos_t *`: pos_t is an array, so a caller
	 * must pass the decayed `pos`, and pos_morton takes a non-const. */
	code = pos_morton(pos);
	rid = xy_region_at(code, ST_PLEN_CELL, &w);
	if (rid == XY_REGION_INVALID)
		return XY_ERR_NOTFOUND;
	*id = rid;
	*plen = w;
	return XY_OK;
}

/* Prefix containment, exactly as xy_region_at() documents it: (oid,oplen)
 * covers (iid,iplen) iff oplen <= iplen and masking iid down to oplen bits
 * yields oid. Pure arithmetic -- no tree walk, no ancestor chain, no row
 * reads -- which is why this is the test the ban guard uses rather than a
 * region lookup: given a destination morton code, "is it under the banned
 * region" is five mask-and-compare operations.
 *
 * plen == 0 needs the shift-by-64 guard: ~0ULL << 64 is undefined, and the
 * cosmos is exactly the case where it would bite. Masking to 0 bits yields 0,
 * so the root covers everything, which is the whole of "the cosmos ruler is
 * world-wide" and needs no special case at either end of this function. */
int
st_region_covers(uint64_t o_id, uint8_t o_plen, uint64_t i_id, uint8_t i_plen)
{
	uint64_t mask;

	if (o_plen > i_plen)
		return 0;
	mask = o_plen ? (~0ULL << (64 - o_plen)) : 0ULL;
	return (i_id & mask) == o_id;
}

/* Is (id,plen) inside the actor's authority?
 *
 * With an explicit selection this is just containment: the selected region and
 * everything under it. With no selection it is the union of every region the
 * actor owns, walked from the persisted rows -- NOT st_can_region, which would
 * let one planet claim the whole address space through the cosmos fallback and
 * turn "my regions" into "everywhere".
 *
 * The cosmos row is (0,0), and (0,0) covers every point, so an actor who rules
 * the cosmos passes every query here without a special case. That is the one
 * place world-wide authority comes from, and it is a consequence of the
 * containment rule rather than a clause bolted on.
 *
 * sel_plen 0 with sel_id 0 is ambiguous -- it is both "the cosmos" and "no
 * selection" -- so callers pass ST_SEL_UNSET to mean the latter. */
int
st_in_scope(unsigned actor, uint64_t sel_id, uint8_t sel_plen, uint64_t id,
	uint8_t plen)
{
	unsigned c;
	const void *kp, *vp;

	if (sel_plen != ST_SEL_UNSET)
		return st_region_covers(sel_id, sel_plen, id, plen);

	if (!st_have_hd())
		return 0;
	c = corm_iter(owner_hd, NULL, 0);
	while (corm_next(&kp, &vp, c)) {
		const char *key = kp;
		const struct st_rec *rec = vp;
		uint64_t rid;
		uint8_t rplen;

		if (st_row_key_parse(key, &rid, &rplen) != 0)
			continue;  /* the ":owner" sidecars, and any legacy key */
		if (rec->plen != rplen || rec->owner != actor)
			continue;
		if (st_region_covers(rid, rplen, id, plen)) {
			corm_fin(c);
			return 1;
		}
	}
	corm_fin(c);
	return 0;
}

/* Region containing an arbitrary object, by walking containment up to the
 * first room.
 *
 * Only rooms are ever mapped (w_hd's sole writer is map_put from st_room_at),
 * so "walk until TYPE_ROOM" is the shortest correct path and needs no
 * positional knowledge about items or entities. Depth is capped against
 * entity cycles -- two entities each holding the other would otherwise spin.
 *
 * An UNMAPPED room resolves to its void coordinates, (0,0,0,0), which is in
 * the cosmos. That is not §27.1's forbidden move. §27.1 is about event
 * anchoring and asks "does this event have a specific anchor?"; its answer for
 * an unmapped room is "no", which falls through to a global dispatch from the
 * root -- the cosmos. This function asks a different question, "whose authority
 * covers this object", and for the void the honest answer is the same region
 * §27.1's fallback lands in. What §27.1 forbids is mistaking the void for a
 * specific PLANET, and this does not: it reports the root, or fails if even the
 * root row is gone.
 *
 * It matters that the void is not "no region". Players log in into an unmapped
 * room, so treating that as unresolvable would put every player who has not yet
 * been moved somewhere under nobody's authority -- a region ruler could not
 * teleport them out, and eng_controls would refuse every command that resolves
 * them by name. The void is part of the cosmos and the cosmos ruler governs it.
 *
 * Returns XY_ERR_NOTFOUND only when the walk genuinely dead-ends: ref 0 /
 * NOTHING, a location with no OBJ row, or no room within the depth cap. Every
 * caller treats that as "not in scope", which is the conservative direction --
 * an object nobody can locate is an object nobody controls. */
int
st_region_of_obj(unsigned ref, uint64_t *id, uint8_t *plen)
{
	unsigned cur = ref;
	int depth;

	if (!id || !plen)
		return XY_ERR_INVALID;

	for (depth = 0; depth < 8; depth++) {
		OBJ o;

		if (cur == 0 || cur == NOTHING)
			return XY_ERR_NOTFOUND;
		/* corm_get_copy returns void, so probe with corm_get first. */
		if (!corm_get(obj_hd, &cur))
			return XY_ERR_NOTFOUND;
		memset(&o, 0, sizeof(o));
		corm_get_copy(obj_hd, &cur, &o);
		if (o.type == TYPE_ROOM) {
			pos_t pos;

			/* No eng_map_has() gate: an unmapped room means the void
			 * (eng_map_where's own comment says "unmapped room:
			 * void/limbo coords"), and the void is the cosmos. See
			 * the note above on why that is not §27.1's trap. */
			eng_map_where(pos, cur);
			/* pos, not &pos: pos_t is coord_t[4], so the name already
			 * decays to coord_t *, which is what the helper takes. */
			return st_region_of_pos(pos, id, plen);
		}
		cur = o.location;
	}
	return XY_ERR_NOTFOUND;
}

/* Region bans (ST.md §27.6(1) §7). Keys are fixed-width strings
 * "PPPPPPPPPP:IIIIIIIIIIIIIIII:PP" (player, hex id, plen): zero-padded so
 * lexicographic order IS (player, id, plen) order, keeping one player's rows
 * contiguous for range scans. A builtin CM_STR key. Exact-key put/get/del
 * are all any caller uses. */
#define BAN_KEY_LEN 32

static void
st_ban_key(char *k, size_t len, unsigned player, uint64_t id, uint8_t plen)
{
	snprintf(k, len, "%010u:%016llx:%02u", player,
		(unsigned long long)id, plen);
}

unsigned ban_hd = (unsigned)-1;
static const char *ban_db = NULL;

static int
st_ban_have(void)
{
	return ban_hd != (unsigned)-1 && ban_hd != CM_MISS;
}

int
st_ban_init(const char *db)
{
	ban_db = db;
	ban_hd = corm_open(db, "region_ban", CM_STR, CM_U32, 0xFFFF, 0);
	if (!st_ban_have())
		WARN("st_ban_init: ban table unavailable, bans unenforced\n");
	return st_ban_have();
}

void
st_ban_put(unsigned player_ref, uint64_t id, uint8_t plen, unsigned banner)
{
	char k[BAN_KEY_LEN];

	if (!st_ban_have())
		return;
	st_ban_key(k, sizeof(k), player_ref, id, plen);
	corm_put(ban_hd, k, &banner);
}

int
st_ban_lookup(unsigned player_ref, uint64_t id, uint8_t plen,
	unsigned *banner)
{
	char k[BAN_KEY_LEN];
	const void *v;

	if (!st_ban_have())
		return 0;
	st_ban_key(k, sizeof(k), player_ref, id, plen);
	v = corm_get(ban_hd, k);
	if (!v)
		return 0;
	if (banner)
		*banner = *(const unsigned *)v;
	return 1;
}

int
st_ban_del(unsigned player_ref, uint64_t id, uint8_t plen)
{
	unsigned banner;

	if (!st_ban_lookup(player_ref, id, plen, &banner))
		return 0;
	{
		char k[BAN_KEY_LEN];

		st_ban_key(k, sizeof(k), player_ref, id, plen);
		corm_del(ban_hd, k);
	}
	return 1;
}

int
st_ban_at(unsigned player_ref, uint64_t id, uint8_t plen,
	uint64_t *ban_id, uint8_t *ban_plen)
{
	if (!st_ban_lookup(player_ref, id, plen, NULL))
		return 0;
	if (ban_id)
		*ban_id = id;
	if (ban_plen)
		*ban_plen = plen;
	return 1;
}

int
st_ban_check(unsigned player_ref, uint64_t code,
	uint64_t *ban_id, uint8_t *ban_plen)
{
	static const uint8_t widths[] = {
		ST_PLEN_ROOT, ST_PLEN_WORLD, 32, 48, ST_PLEN_CELL
	};
	size_t i;

	if (!st_ban_have())
		return 0;
	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		uint64_t mask = widths[i] ? (~0ULL << (64 - widths[i])) : 0ULL;

		if (st_ban_at(player_ref, code & mask, widths[i],
				ban_id, ban_plen))
			return 1;
	}
	return 0;
}

/* The retired EF_BAN bit, kept as a literal for the migration read below.
 * The enum member is gone (ST.md §27.6(1) §7 supersedes the bit with this
 * table); nothing sets bit 16 anymore, so any row still carrying it is a ban
 * from before the upgrade. */
#define EF_BAN_LEGACY 16

void
st_ban_migrate(void)
{
	unsigned c;
	const void *kp, *vp;

	if (!st_ban_have())
		return;
	c = corm_iter(ent_hd, NULL, 0);
	while (corm_next(&kp, &vp, c)) {
		unsigned ref = *(const unsigned *)kp;
		ENT e = *(const ENT *)vp;

		if (!(e.flags & EF_BAN_LEGACY))
			continue;
		/* Banner unknown for legacy bans: NOTHING, which no live ref can
		 * collide with. Region rulers can still unban through the region
		 * rule; nobody can claim to be the original banner. */
		if (!st_ban_have())
			continue;
		st_ban_put(ref, 0, ST_PLEN_ROOT, NOTHING);
		e.flags &= ~EF_BAN_LEGACY;
		eng_ent_set(ref, &e);
	}
	corm_fin(c);
}

/* Name a banned place for messages: the world number when it has one, the
 * honest "everywhere" for a cosmos ban, "this region" otherwise. Shared by
 * the ban/unban confirmations and the entry refusal so the place never has
 * three spellings. */
void
st_ban_place(uint64_t id, uint8_t plen, char *buf, size_t len)
{
	if (plen == ST_PLEN_ROOT)
		snprintf(buf, len, "everywhere");
	else if (plen == ST_PLEN_WORLD)
		snprintf(buf, len, "world %u", st_world_of(id));
	else
		snprintf(buf, len, "this region");
}

void
st_ban_refuse(unsigned player_ref, uint64_t id, uint8_t plen)
{
	char place[64];

	st_ban_place(id, plen, place, sizeof(place));
	nd_writef(player_ref, "You are banned from %s.\n", place);
	eng_nd_flush(player_ref);
}

/* The ruler's display name for a row header: the OBJ name when the ref still
 * resolves, otherwise the bare number. corm_get_copy returns void, so
 * existence is tested first -- a released owner's ref must not print garbage. */
static void
st_owner_name(unsigned owner_ref, char *buf, size_t len)
{
	OBJ o;

	if (corm_get(obj_hd, &owner_ref)) {
		memset(&o, 0, sizeof(o));
		corm_get_copy(obj_hd, &owner_ref, &o);
		snprintf(buf, len, "%s", o.name);
	} else {
		snprintf(buf, len, "%u", owner_ref);
	}
}

/* The one refusal message every region gate uses, so "why" never depends on
 * which command you typed. do_room already had this shape inline; it is here
 * so the six gates do not each invent a wording, and because naming the ruler
 * is only possible if the caller can ask who the ruler is.
 *
 * Flushes: every gate that calls this returns immediately afterwards, and
 * nd_writef buffers per fd until the NEXT command (io.c), so an unflushed
 * refusal is indistinguishable from silence -- which is how five of the six
 * commands managed to look like no-ops for so long (ST.md §27.6(1)). */
void
st_refuse_region(unsigned player_ref, uint64_t id, uint8_t plen)
{
	char oname[64];
	unsigned ruler = st_region_ruler(id, plen);

	if (ruler == NOTHING)
		nd_writef(player_ref, "Permission denied (this area is unclaimed)\n");
	else {
		st_owner_name(ruler, oname, sizeof(oname));
		nd_writef(player_ref, "Permission denied (ruled by %s)\n", oname);
	}
	eng_nd_flush(player_ref);
}

static void
st_row_header(unsigned player_ref, uint64_t id, uint8_t plen,
	const struct st_rec *rec)
{
	char oname[64];

	st_owner_name(rec->owner, oname, sizeof(oname));
	if (plen == ST_PLEN_WORLD) {
		nd_writef(player_ref, "[id=0x%016llx plen=%u world=%u owner=%s mods=%u]\n",
			(unsigned long long)id, plen, st_world_of(id),
			oname, rec->nmods);
	} else {
		nd_writef(player_ref, "[id=0x%016llx plen=%u owner=%s mods=%u]\n",
			(unsigned long long)id, plen, oname, rec->nmods);
	}
}

/* CMD_REGION.md §5.3: ONE region selector token, shared by every command that
 * takes one plus `target`. `cosmos` is the root and is deliberately NOT
 * `world 0`: st_planet_id(0) is (0, ST_PLEN_WORLD) = (0, 16), the first child of
 * the cosmos, and libxylem is explicit that "(0,0) and (0,16) share" an id.
 * Without this keyword the cosmos would be unreachable from these commands. */
int
st_cmd_world(const char *tok, uint64_t *id, uint8_t *plen)
{
	char *end = NULL;
	unsigned long w;

	if (!tok || !*tok || !id || !plen)
		return XY_ERR_INVALID;
	if (strcmp(tok, "cosmos") == 0) {
		*id = 0;
		*plen = ST_PLEN_ROOT;
		return XY_OK;
	}
	/* The *end check rejects trailing junk. strtoul also tolerates leading
	 * whitespace and a leading '+', which is harmless here: the caller's
	 * tokens arrive already split on whitespace. */
	w = strtoul(tok, &end, 10);
	if (!end || end == tok || *end || w > 65535)
		return XY_ERR_INVALID;
	*id = st_planet_id((unsigned)w);
	*plen = ST_PLEN_WORLD;
	return XY_OK;
}

/* CMD_REGION.md §4.1: the sentinel is translated HERE and never returned. See
 * st.h for why that discipline is load-bearing rather than tidiness. */
int
st_target_get(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	ENT e = eng_ent_get(player_ref);

	if (e.target_plen == ST_SEL_UNSET)
		return 0;
	if (id)
		*id = e.target_id;
	if (plen)
		*plen = e.target_plen;
	return 1;
}

/* Read-modify-write, like every other ENT mutation in the engine: the row is
 * re-read before the write, so no field is clobbered by a stale copy. */
void
st_target_set(unsigned player_ref, uint64_t id, uint8_t plen)
{
	ENT e = eng_ent_get(player_ref);

	e.target_id = id;
	e.target_plen = plen;
	eng_ent_set(player_ref, &e);
}

void
st_target_clear(unsigned player_ref)
{
	st_target_set(player_ref, 0, ST_SEL_UNSET);
}

/* CMD_REGION.md §4.3: THE default resolution. Explicit argument -> the player's
 * default target -> the player's position. Seven commands route through this,
 * which is the whole point: before it, `wall` resolved its no-argument case
 * directly and would have ignored a setting the other six honoured. */
int
st_target_or_position(unsigned player_ref, uint64_t *id, uint8_t *plen)
{
	if (st_target_get(player_ref, id, plen))
		return XY_OK;
	return st_region_of_player(player_ref, id, plen);
}

/* argv[world_arg] names a region outright; without it the caller's default
 * target region, or failing that their position-derived region. Returns XY_OK
 * with (*id, *plen) set, or a negative XY_ERR_* with nothing set. */
int
st_cmd_region(unsigned player_ref, int argc, char *argv[], int world_arg,
	uint64_t *id, uint8_t *plen)
{
	if (argc > world_arg && argv[world_arg] && *argv[world_arg]) {
		if (st_cmd_world(argv[world_arg], id, plen) == XY_OK)
			return XY_OK;
		return XY_ERR_INVALID;
	}
	return st_target_or_position(player_ref, id, plen);
}

void
do_planet(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	uint64_t id;
	uint8_t plen = ST_PLEN_WORLD;
	struct st_rec rec;
	int have;
	char *end = NULL;
	unsigned long w;
	char oname[64];

	if (argc < 2 || !argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: planet <world>\n");
		eng_nd_flush(player_ref);
		return;
	}
	w = strtoul(argv[1], &end, 10);
	if (!end || *end || w > 65535) {
		nd_writef(player_ref, "Invalid world (0-65535)\n");
		eng_nd_flush(player_ref);
		return;
	}
	id = st_planet_id((unsigned)w);

	have = st_row_get(id, plen, &rec);
	if (have && rec.owner != player_ref && rec.owner != NOTHING) {
		st_owner_name(rec.owner, oname, sizeof(oname));
		nd_writef(player_ref, "Planet %lu is already claimed by %s\n",
			w, oname);
		eng_nd_flush(player_ref);
		return;
	}
	/* A new claim is a cosmos-level decision: only the cosmos ruler may
	 * carve a planet out of it. A reclaim only needs the row to be
	 * unclaimed or the actor's own. */
	if (!have && !st_can(player_ref, 0, ST_PLEN_ROOT)) {
		nd_writef(player_ref, "Permission denied\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (xy_claim_at(id, plen, NULL, NULL) != XY_OK) {
		nd_writef(player_ref, "Planet %lu could not be claimed (%s)\n",
			w, xy_strerror(xy_errno()));
		eng_nd_flush(player_ref);
		return;
	}
	/* A reclaim keeps the module set: the new ruler inherits the planet's
	 * code and unloads what they do not want. */
	if (!have) {
		memset(&rec, 0, sizeof(rec));
		rec.plen = plen;
	}
	rec.owner = player_ref;
	st_row_put(id, plen, &rec);

	st_owner_name(player_ref, oname, sizeof(oname));
	nd_writef(player_ref,
		"planet %lu established: region id=0x%016llx plen=%u world=%lu owner=%s mods=%u\n",
		w, (unsigned long long)id, plen, w, oname, rec.nmods);
	eng_nd_flush(player_ref);
}

void
do_planets(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd);
	unsigned c = corm_iter(owner_hd, NULL, 0);
	const void *kp, *vp;
	int n = 0;

	if (!st_have_hd())
		return;
	while (corm_next(&kp, &vp, c)) {
		const char *key = kp;
		const struct st_rec *rec = vp;
		uint64_t id;
		uint8_t plen;

		if (st_row_key_parse(key, &id, &plen) != 0)
			continue;
		if (rec->plen != plen)
			continue;
		st_row_header(player_ref, id, plen, rec);
		n++;
	}
	corm_fin(c);
	if (!n)
		nd_writef(player_ref, "No regions claimed.\n");
	eng_nd_flush(player_ref);
}

void
do_here(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd);
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;

	if (st_region_of_player(player_ref, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "You are nowhere.\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		memset(&rec, 0, sizeof(rec));
		rec.owner = NOTHING;
		rec.plen = plen;
	}
	st_row_header(player_ref, id, plen, &rec);
	eng_nd_flush(player_ref);
}

/* CMD_REGION.md §6: the player's default target region -- the region that
 * wall/ban/unban/loadmod/unloadmod/modlist/deny act on when no selector is
 * given. Bare `target` PRINTS (it is the discovery path for the whole feature,
 * so it also says what it affects); `select` by contrast sets unconditionally. */
void
do_target(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	uint64_t id;
	uint8_t plen;
	char place[64];
	const char *arg = (argc > 1 && argv[1]) ? argv[1] : "";

	if (!*arg) {
		if (st_target_get(player_ref, &id, &plen)) {
			st_ban_place(id, plen, place, sizeof(place));
			nd_writef(player_ref,
				"Default target region: %s.\n", place);
			nd_writef(player_ref,
				"Commands without a region act there: wall, ban, "
				"unban, loadmod, unloadmod, modlist, deny.\n");
		} else {
			nd_writef(player_ref,
				"No default target region set (commands act on "
				"your current region).\n");
			nd_writef(player_ref,
				"Set one with `target <region>', `target here', "
				"or clear it with `target none'.\n");
		}
		eng_nd_flush(player_ref);
		return;
	}

	if (strcmp(arg, "none") == 0) {
		st_target_clear(player_ref);
		nd_writef(player_ref, "Default target region cleared.\n");
		eng_nd_flush(player_ref);
		return;
	}

	/* `target here` stores the region the player is standing in -- whatever
	 * st_region_of_player() resolves, which is the deepest CLAIMED region, not
	 * necessarily a cell. Only worlds are claimed (via `planet`), so in practice
	 * this is a world or the cosmos. A raw cell (the full 64-bit position) is
	 * deliberately NOT stored: recipients resolve to world granularity, so a
	 * cell selector would cover nobody and the default would be a setting that
	 * refuses everything it touches.
	 *
	 * This is still more than a `target <n>` alias: it needs no world number,
	 * and from the void it stores the cosmos, which no number can name. */
	if (strcmp(arg, "here") == 0) {
		if (st_region_of_player(player_ref, &id, &plen) != XY_OK) {
			nd_writef(player_ref, "You are nowhere.\n");
			eng_nd_flush(player_ref);
			return;
		}
	} else if (st_cmd_world(arg, &id, &plen) != XY_OK) {
		nd_writef(player_ref,
			"Usage: target [<world> | cosmos | here | none]\n");
		eng_nd_flush(player_ref);
		return;
	}

	/* Authorization at SET time, not at use time (CMD_REGION.md §6.3): a default
	 * the player cannot use is a setting that appears to work and then refuses
	 * every command -- the silent-no-op class of bug NO_WIZ.md §13.3 documents.
	 * st_can_region (not the exact-ownership st_can that loadmod/unloadmod use)
	 * because that asymmetry is honest: module loading attaches to one specific
	 * region row, while a wall or ban is scoped to a subtree. A default naming a
	 * descendant the player does not own exactly is settable here and refused by
	 * those two commands, which is correct rather than surprising. */
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}
	st_target_set(player_ref, id, plen);
	st_ban_place(id, plen, place, sizeof(place));
	nd_writef(player_ref, "Default target region set to %s.\n", place);
	eng_nd_flush(player_ref);
}

void
do_room(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	pos_t pos;
	unsigned there_ref;
	uint64_t rid;
	uint8_t rplen;
	char oname[64];
	char *end;
	long v;
	int i;

	/* §27.3: this is the ENABLING primitive for the Phase 3 gate. Every room
	 * inherits pos[3] from the room it was carved from (st_pos -> pos_move),
	 * so the fresh world's rooms all sit at pos[3] == 0 and NOTHING in-game
	 * reaches a non-zero world -- while "a module in planet A must not fire
	 * for an event anchored in planet B" needs an anchor *in* planet B.
	 *
	 * It creates-or-finds the room AND enters it. The entering part is not
	 * optional: do_teleport cannot make this move for a non-wizard because
	 * its eng_controls path requires control of the caller's current
	 * location, while room is already authorized for the target region. This
	 * is the same create-or-find-then-enter shape as eng_st_teleport.
	 *
	 * Coordinates are explicit rather than derived, which is the whole point:
	 * deriving them would inherit the world being 0 and reproduce the bug.
	 */
	if (argc < 5) {
		nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
		eng_nd_flush(player_ref);
		return;
	}

	/* strtol, not strtoul, and the range is checked: pos_t is signed and
	 * pos_morton() reads it as unsigned, so -1 and 65535 would be two names
	 * for the same cell. Refusing both is the only way the printed
	 * coordinates can be read back as the coordinates that were stored. */
	for (i = 0; i < 4; i++) {
		if (!argv[1 + i] || !*argv[1 + i]) {
			nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
			eng_nd_flush(player_ref);
			return;
		}
		v = strtol(argv[1 + i], &end, 10);
		if (end == argv[1 + i] || *end || v < COORD_MIN || v > COORD_MAX) {
			nd_writef(player_ref, "Usage: room <x> <y> <z> <w>\n");
			eng_nd_flush(player_ref);
			return;
		}
		pos[i] = (coord_t)v;
	}

	/* Authorize against the region the TARGET position falls in, not the one
	 * the caller is standing in -- otherwise this would be a room-creating
	 * privilege for anyone who happened to be anywhere, and would not stop a
	 * planet's ruler from reaching into their neighbour. See st_can_region for
	 * why the cosmos clause exists. */
	if (st_region_of_pos(pos, &rid, &rplen) != XY_OK) {
		nd_writef(player_ref, "Couldn't place a room there.\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, rid, rplen)) {
		st_owner_name(rid == 0 ? 0 : st_owner(rid, rplen), oname,
			sizeof(oname));
		nd_writef(player_ref, "Permission denied (ruled by %s)\n", oname);
		eng_nd_flush(player_ref);
		return;
	}

	/* Create-or-find. Asking the map first means the command is idempotent,
	 * which matters because the gate calls it on both boots: re-creating a
	 * room would hand out a fresh ref every time and any stored ref would
	 * silently dangle. */
	there_ref = eng_map_get(pos);
	if (there_ref != NOTHING) {
		const void *ov = corm_get(obj_hd, &there_ref);
		if (ov && ((const OBJ *)ov)->type == TYPE_ROOM) {
			eng_enter(player_ref, there_ref, E_NULL);
			nd_writef(player_ref, "room %u at %d %d %d %d (existing)\n",
				there_ref, pos[0], pos[1], pos[2], pos[3]);
			eng_nd_flush(player_ref);
			return;
		}
	}

	there_ref = st_room_at(player_ref, pos);
	if (there_ref == NOTHING) {
		nd_writef(player_ref, "Couldn't make a room there.\n");
		eng_nd_flush(player_ref);
		return;
	}
	eng_enter(player_ref, there_ref, E_NULL);
	nd_writef(player_ref, "room %u at %d %d %d %d\n",
		there_ref, pos[0], pos[1], pos[2], pos[3]);
	eng_nd_flush(player_ref);
}

struct st_deny_arg {
	char what[ND_ST_MOD_NAME + 32];
	xy_deny_type_t type;
	int rc;
};

static int
st_deny_tramp(void *ud)
{
	struct st_deny_arg *a = ud;

	a->rc = xy_deny(a->what, a->type);
	return XY_OK;
}

/* deny <hook|module> <name> [world] -- ST.md §4.x delegation.
 *
 * SEMANTICS, which are dispatch-time and not load-time: libxylem consults a
 * module deny only from the dispatch walker (module_is_denied() is reached
 * from libxylem-dispatch.c and nowhere else), so a denied module still LOADS
 * and is still recorded in the region's set -- it simply never gets to run
 * there. That is the honest reading of xy.h's "a module that may not be loaded
 * inside this region's subtree" only in the sense that matters, and it is what
 * the Phase 3 gate asserts.
 *
 * There is NO removal API for a deny (xy_deny only prepends), so a deny is
 * permanent for the life of the process and does not survive a reboot -- the
 * deny sets live in region entries, and regions are rebuilt from the st rows.
 * A "release" therefore cannot lift one either.
 */
void
do_deny(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	struct st_deny_arg a;
	uint64_t id;
	uint8_t plen;
	xy_deny_type_t type;
	int rc;

	if (argc < 3 || !argv[1] || !*argv[1] || !argv[2] || !*argv[2]) {
		nd_writef(player_ref, "Usage: deny <hook|module> <name> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!strcmp(argv[1], "hook"))
		type = XY_DENY_HOOK;
	else if (!strcmp(argv[1], "module"))
		type = XY_DENY_MODULE;
	else {
		nd_writef(player_ref, "Usage: deny <hook|module> <name> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (st_cmd_region(player_ref, argc, argv, 3, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Invalid world\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		nd_writef(player_ref, "Permission denied\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, NULL)) {
		/* A deny is a statement about a region's SUBTREE, so unlike `room`
		 * there is no bootstrap case: there is no subtree to restrict until
		 * the region exists. Refusing here keeps `deny` from silently
		 * creating a region row as a side effect of a permission check. */
		nd_writef(player_ref, "No such region\n");
		eng_nd_flush(player_ref);
		return;
	}

	snprintf(a.what, sizeof(a.what), "%s", argv[2]);
	a.type = type;
	a.rc = XY_ERR_INVALID;

	/* claim first (idempotent, and it makes the region current), then
	 * ADDRESS it explicitly -- the same claim-then-with_region shape as
	 * st_region_load, rather than a reliance on retained currentness. */
	rc = xy_claim_at(id, plen, NULL, NULL);
	if (rc == XY_OK)
		rc = xy_with_region(id, plen, st_deny_tramp, &a);
	if (rc != XY_OK || a.rc != XY_OK) {
		nd_writef(player_ref, "deny failed (%s)\n",
			xy_strerror(rc != XY_OK ? rc : a.rc));
		eng_nd_flush(player_ref);
		return;
	}
	/* Report the FULL identity, not just the world: a cell-level deny is
	 * legal and "denied in planet N" would name the wrong region for one. */
	nd_writef(player_ref, "denied: %s %s in region id=0x%016llx plen=%u\n",
		type == XY_DENY_HOOK ? "hook" : "module", argv[2],
		(unsigned long long)id, plen);
	eng_nd_flush(player_ref);
}

void
do_loadmod(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char namebuf[ND_ST_MOD_NAME + 1];
	uint64_t id;
	uint8_t plen;
	int rc;

	if (argc < 2 || !argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: loadmod <name> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (strchr(argv[1], '/') || strlen(argv[1]) >= ND_ST_MOD_NAME) {
		nd_writef(player_ref, "Invalid module name (stems only)\n");
		eng_nd_flush(player_ref);
		return;
	}
	snprintf(namebuf, sizeof(namebuf), "%s", argv[1]);

	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Invalid world\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can(player_ref, id, plen)) {
		nd_writef(player_ref, "Permission denied\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, NULL)) {
		nd_writef(player_ref, "No such region\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (st_mod_loaded(id, plen, namebuf)) {
		nd_writef(player_ref, "%s is already loaded\n", namebuf);
		eng_nd_flush(player_ref);
		return;
	}
	/* Load FIRST, record after: a failed load leaves the row exactly as it
	 * was, so a typo cannot persist and haunt every boot (§22.5). */
	rc = st_region_load(id, plen, namebuf);
	if (rc != XY_OK) {
		nd_writef(player_ref, "%s failed to load (%s)\n",
			namebuf, xy_strerror(xy_errno()));
		eng_nd_flush(player_ref);
		return;
	}
	st_mod_add(id, plen, namebuf);
	nd_writef(player_ref, "%s loaded into region id=0x%016llx plen=%u\n",
		namebuf, (unsigned long long)id, plen);
	eng_nd_flush(player_ref);
}

void
do_unloadmod(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char namebuf[ND_ST_MOD_NAME + 1];
	uint64_t id;
	uint8_t plen;
	int rc;

	if (argc < 2 || !argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: unloadmod <name> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (strchr(argv[1], '/') || strlen(argv[1]) >= ND_ST_MOD_NAME) {
		nd_writef(player_ref, "Invalid module name (stems only)\n");
		eng_nd_flush(player_ref);
		return;
	}
	snprintf(namebuf, sizeof(namebuf), "%s", argv[1]);

	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Invalid world\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can(player_ref, id, plen)) {
		nd_writef(player_ref, "Permission denied\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_mod_loaded(id, plen, namebuf)) {
		nd_writef(player_ref, "%s is not loaded\n", namebuf);
		eng_nd_flush(player_ref);
		return;
	}
	rc = st_region_unload(id, plen, namebuf);
	if (rc != XY_OK && rc != XY_ERR_NOTFOUND) {
		nd_writef(player_ref, "%s failed to unload (%s)\n",
			namebuf, xy_strerror(xy_errno()));
		eng_nd_flush(player_ref);
		return;
	}
	st_mod_del(id, plen, namebuf);
	nd_writef(player_ref, "%s unloaded from region id=0x%016llx plen=%u\n",
		namebuf, (unsigned long long)id, plen);
	eng_nd_flush(player_ref);
}

void
do_modlist(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;
	uint32_t i;

	if (st_cmd_region(player_ref, argc, argv, 1, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Invalid world\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		nd_writef(player_ref, "No such region\n");
		eng_nd_flush(player_ref);
		return;
	}
	st_row_header(player_ref, id, plen, &rec);
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++)
		nd_writef(player_ref, "  %s\n", rec.mods[i]);
	eng_nd_flush(player_ref);
}

void
do_release(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	uint64_t id;
	uint8_t plen;
	struct st_rec rec;
	uint32_t i;
	char namebuf[ND_ST_MOD_NAME + 1];
	char *end = NULL;
	unsigned long w;

	if (argc < 2 || !argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: release <world>\n");
		eng_nd_flush(player_ref);
		return;
	}
	w = strtoul(argv[1], &end, 10);
	if (!end || *end || w > 65535) {
		nd_writef(player_ref, "Invalid world (0-65535)\n");
		eng_nd_flush(player_ref);
		return;
	}
	id = st_planet_id((unsigned)w);
	plen = ST_PLEN_WORLD;

	if (!st_can(player_ref, id, plen)) {
		nd_writef(player_ref, "Permission denied\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_row_get(id, plen, &rec)) {
		nd_writef(player_ref, "No such region\n");
		eng_nd_flush(player_ref);
		return;
	}
	/* Best effort: one stuck module must not veto the release of the rest.
	 * The row is deleted regardless, so nothing unloaded here comes back. */
	for (i = 0; i < rec.nmods && i < ND_ST_MAX_MODS; i++) {
		memcpy(namebuf, rec.mods[i], ND_ST_MOD_NAME);
		namebuf[ND_ST_MOD_NAME] = '\0';
		if (!namebuf[0])
			continue;
		if (st_region_unload(id, plen, namebuf) != XY_OK)
			nd_writef(player_ref, "%s would not unload (%s)\n",
				namebuf, xy_strerror(xy_errno()));
	}
	st_row_del(id, plen);
	/* The region entry itself stays, inert: region entries are never
	 * destroyed, and a later `planet` with the same number reuses it (§4.6). */
	nd_writef(player_ref, "planet %lu released\n", w);
	eng_nd_flush(player_ref);
}
