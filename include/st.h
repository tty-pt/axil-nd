#ifndef SPACETIME_H
#define SPACETIME_H

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ttypt/corm.h>
#include "uapi/object.h"
#include "uapi/st.h"

// adds 2^DAYTICK_Y to day tick until it reaches DAYSIZE
#define DAYTICK_Y	10
#define NIGHT_IS	(day_n == 1)

#define Y_COORD 0
#define X_COORD 1

#define COORD_MIN SHRT_MIN
#define COORD_MAX SHRT_MAX
#define UCOORD_MAX USHRT_MAX

#define DIM 2
#define WDIM X_COORD

#define OBITS(code) (code >> 48)

#define VIEW_AROUND 3
#define VIEW_SIZE ((VIEW_AROUND<<1) + 1)
#define VIEW_M VIEW_SIZE * VIEW_SIZE
#define VIEW_BDI (VIEW_SIZE * (VIEW_SIZE - 1))
#define MORTON_READ(pos) (* (morton_t *) pos)

typedef ucoord_t upoint_t[DIM];

typedef coord_t point3D_t[3];
typedef ucoord_t upoint3D_t[3];
typedef ucoord_t upoint4D_t[4];

typedef int64_t smorton_t;

struct rect {
	point_t s;
	upoint_t l;
};

struct rect4D {
	point4D_t s;
	upoint4D_t l;
};

typedef struct {
	const char name[16];
	const char other[16];
	enum exit simm;
	coord_t dim, dis;
} exit_t;

extern unsigned long long day_tick;
extern unsigned short day_n;
extern double tick;
extern enum exit e_map[];
extern exit_t exit_map[];

time_t get_tick(void);

morton_t pos_morton(pos_t);
void morton_pos(pos_t p, morton_t code);

int e_exit_can(OBJ *player, enum exit e);
int e_ground(unsigned room, enum exit e);

void pos_move(pos_t d, pos_t o, enum exit e);
enum exit dir_e(const char dir);
char e_dir(enum exit e);
enum exit e_simm(enum exit e);
char *e_name(enum exit e);
char *e_other(enum exit e);
morton_t point_rel_idx(point_t p, point_t s, smorton_t w);

void st_update(double dt);
int st_v(unsigned player_ref, const char *dir);

/* ST.md §22.1: a region is (id, plen), never id alone -- four regions share
 * id 0 (the root and its leftmost descendants at every width). plen is the
 * prefix width in the HIGH bits of id, so `shift` in the old st_key was
 * 64 - plen throughout; the shift-based spelling is kept only where the
 * legacy stchown/streload commands still speak it (§22.8). */
#define ST_SHIFT(plen) (64 - (unsigned)(plen))
#define ST_PLEN_ROOT  0    /* the cosmos: the whole keyspace */
#define ST_PLEN_WORLD 16   /* one world -- a planet */
#define ST_PLEN_CELL  64   /* one cell */

/* "No region was selected", as a plen. Distinct from ST_PLEN_ROOT on purpose:
 * (0,0) names the cosmos, a real region with a real owner, and collapsing the
 * two would make an explicit `... world 0` indistinguishable from omitting the
 * argument -- which matters because st_in_scope() treats them differently
 * (cosmos = that one region; unset = every region the actor rules). 255 is not
 * a legal plen (a prefix width is 0..64), so it cannot collide. */
#define ST_SEL_UNSET  255

#define ND_ST_MAX_MODS 32
#define ND_ST_MOD_NAME 24

/* One corm row per region: owner, width, and the moderator's module set.
 *
 * owner/plen/nmods/flags are uint32_t rather than the uint8_t §7.2 sketched
 * because a CM_U32 corm record field memcpy's four bytes (libcorm.c:1517), so
 * a narrower field would be read past its own storage. mods is the flat
 * ND_ST_MAX_MODS x ND_ST_MOD_NAME block, addressed by nmods -- there is no
 * cross-element NUL guarantee, so nmods is the only safe bound (§22.2). */
struct st_rec {
	uint32_t owner;   /* OBJ ref of the ruler; NOTHING = unclaimed */
	uint32_t plen;    /* cross-check against the key's plen suffix (§22.2) */
	uint32_t nmods;   /* authoritative for iteration */
	uint32_t flags;   /* reserved */
	char     mods[ND_ST_MAX_MODS][ND_ST_MOD_NAME];
};

/* corm key for a region row: "<16 hex id><2 hex plen>", fixed width 18.
 *
 * Fixed width is what makes it parseable without sscanf's greedy-field
 * ambiguity, and parseable is required: a row must re-derive its own (id,
 * plen) to hand to xy_claim_at()/xy_with_region(), and a CM_U32 field is four
 * bytes so a uint64 id does not fit the field API at all. */
#define ST_ROW_KEY_LEN 18

static inline void
st_row_key(char *buf, size_t len, uint64_t id, uint8_t plen)
{
	snprintf(buf, len, "%016llx%02x", (unsigned long long)id, (unsigned)plen);
}

/* Hand-rolled rather than sscanf("%16llx%2x"): the width caps make that work
 * only if both conversions fill their field, and a short key would then be
 * silently accepted with a stale high word. */
static inline int
st_row_key_parse(const char *key, uint64_t *id, uint8_t *plen)
{
	unsigned long long v = 0;
	unsigned p = 0;

	if (!key || strlen(key) != ST_ROW_KEY_LEN)
		return -1;

	for (int i = 0; i < 16; i++) {
		char c = key[i];
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		v = (v << 4) | (unsigned long long)d;
	}
	for (int i = 16; i < ST_ROW_KEY_LEN; i++) {
		char c = key[i];
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		p = (p << 4) | (unsigned)d;
	}
	if (p > 64)
		return -1;

	*id = v;
	*plen = (uint8_t)p;
	return 0;
}

/* A planet IS a world: the complete 16-bit slice of the 4th coordinate is the
 * top 16 bits of pos_morton, so world N is the prefix region (N<<48, 16)
 * (§1.10, §2.3). */
static inline uint64_t st_planet_id(unsigned world)
{
	return (uint64_t)world << 48;
}

static inline unsigned st_world_of(uint64_t id)
{
	return (unsigned)(id >> 48);
}

void st_init(void);
void do_planet(int fd, int argc, char *argv[]);
void do_planets(int fd, int argc, char *argv[]);
void do_here(int fd, int argc, char *argv[]);
void do_loadmod(int fd, int argc, char *argv[]);
void do_unloadmod(int fd, int argc, char *argv[]);
void do_modlist(int fd, int argc, char *argv[]);
void do_release(int fd, int argc, char *argv[]);

/* Phase 2 region API (ST.md §22). Everything here speaks (id, plen). */

/* Register the st_rec record layout. Call once from nd_world_init BEFORE the
 * `st` table opens; the returned id feeds CM_RECORD(id) and
 * corm_record_type_id(). Idempotent. */
uint32_t st_rec_register(void);

/* One-field read of the owner -- up to 65 regions can cover one position and
 * this must not copy a whole 784-byte row for each probe (§22.1). */
unsigned st_owner(uint64_t id, uint8_t plen);
int st_can(unsigned ref, uint64_t id, uint8_t plen);

/* The one authorization rule for region-scoped commands: you may act in a
 * region you rule, or in anything under a region you rule. Was static until
 * ST.md §27.6(1), when the nine dead EF_WIZARD gates were re-pointed at region
 * ownership and needed it from four other translation units.
 *
 * "Ruler above it" is spelled out rather than left implicit: the candidate
 * ancestor widths are walked explicitly, so a row appearing at plen 32 or 48
 * is honoured the day it exists instead of being silently ignored. Today only
 * widths 0 (cosmos) and 16 (planet) can hold a row, so exact-owner and the
 * cosmos fallback are the same two iterations. */
int st_can_region(unsigned player_ref, uint64_t id, uint8_t plen);

/* Who governs (id,plen): the deepest ancestor row that has an owner, walking
 * the same widths as st_can_region in the opposite direction. NOTHING means no
 * ancestor has a row -- "unclaimed", which is a different refusal from "ruled
 * by somebody else". */
unsigned st_region_ruler(uint64_t id, uint8_t plen);

/* The shared region refusal: names the ruler (or says the area is unclaimed)
 * and flushes. Every gate in ST.md §27.6(1) returns through this so the
 * wording and the flush are not re-derived six times. */
void st_refuse_region(unsigned player_ref, uint64_t id, uint8_t plen);

/* Prefix containment, the identity xy_region_at() documents: (oid,oplen)
 * covers (iid,iplen) iff oplen <= iplen and masking iid to oplen yields oid.
 * Pure arithmetic -- no tree walk, no ancestor chain, no row reads. */
int st_region_covers(uint64_t o_id, uint8_t o_plen, uint64_t i_id,
	uint8_t i_plen);

/* Is (id,plen) inside the actor's authority? With an explicit selection
 * (sel_plen != ST_SEL_UNSET): the selected region and everything under it.
 * With no selection: the union of every region the actor owns. Note the cosmos
 * row is (0,0), which covers the whole address space, so an actor who rules the
 * cosmos is world-wide for free, with no special case.
 *
 * sel_plen == 0 with sel_id == 0 is a real region (the cosmos), so it cannot
 * double as "unspecified" -- hence ST_SEL_UNSET. */
int st_in_scope(unsigned actor, uint64_t sel_id, uint8_t sel_plen,
	uint64_t id, uint8_t plen);

/* Region containing an arbitrary object: walk containment up to the first
 * room, then take that room's morton code. An UNMAPPED room yields the void
 * (0,0,0,0), which is in the cosmos -- players log in into unmapped rooms, so
 * calling that "unresolvable" would put every such player under nobody's
 * authority. See the long note in src/spacetime.c.
 *
 * Returns non-zero (and leaves the out-params alone) only when the walk
 * genuinely dead-ends: ref 0/NOTHING, a location with no OBJ row, or no room
 * within the depth cap. That reads as "not in scope" at every caller, which is
 * the conservative direction: an object nobody can locate is an object nobody
 * controls. */
int st_region_of_obj(unsigned ref, uint64_t *id, uint8_t *plen);

int st_row_get(uint64_t id, uint8_t plen, struct st_rec *out);
void st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec);
void st_row_del(uint64_t id, uint8_t plen);

/* Create-or-reuse the region, then load/unload a module inside it. Both return
 * an XY_OK / XY_ERR_* code. A failed load never touches the row (§7.6). */
int st_region_load(uint64_t id, uint8_t plen, char *name);
int st_region_unload(uint64_t id, uint8_t plen, char *name);
int st_mod_loaded(uint64_t id, uint8_t plen, const char *name);

/* Add/remove a stem from the persisted set. st_mod_add does NOT load; the
 * caller loads first so a failure leaves the intent recorded (§7.6). */
int st_mod_add(uint64_t id, uint8_t plen, const char *name);
int st_mod_del(uint64_t id, uint8_t plen, const char *name);

/* The caller's position-derived region: pos_morton of the room's pos_t, then
 * xy_region_at. Deliberately NOT eng_map_mwhere -- that reinterprets the 8
 * pos_t bytes as a uint64 rather than Morton-encoding them (§22.4). */
int st_region_of_player(unsigned player_ref, uint64_t *id, uint8_t *plen);

/* Region bans (ST.md §27.6(1) §7): a separate persisted table keyed
 * (player, id, plen), enforced at every eng_enter() arrival. The value is the
 * banning ruler's ref, so unban can re-check authority. */
extern unsigned ban_hd;

/* Opens the ban table on db. The table must open HERE, at boot alongside
 * the other engine tables: measured (Oct 2026) that a table opened later in
 * the process's life (lazy open on first ban) persists through explicit
 * mid-life `save` but loses its rows at SIGTERM shutdown, while a
 * boot-opened table survives save, shutdown, and reboot alike. A failed open
 * degrades to unenforced bans, the same shape as a missing region table. */
int st_ban_init(const char *db);

/* One-time boot migration: any entity still carrying the retired EF_BAN bit
 * becomes a root-wide (0,0) row, and the bit is cleared so the migration does
 * not repeat. Without it an upgrade silently drops every ban ever issued. */
void st_ban_migrate(void);

/* Is player excluded from (id,plen)? Probes the ban table for the player's
 * row at that exact region. Returns non-zero with (*ban_id, *ban_plen) set to
 * the banning row on a hit. */
int st_ban_at(unsigned player_ref, uint64_t id, uint8_t plen,
	uint64_t *ban_id, uint8_t *ban_plen);

/* Is code (a destination morton) under any of player's bans? Masks the code
 * down to each of the 5 widths and probes; the mask IS the containment test,
 * so no region lookup is needed. Returns non-zero with the banning row set. */
int st_ban_check(unsigned player_ref, uint64_t code,
	uint64_t *ban_id, uint8_t *ban_plen);

/* Write / remove one row. st_ban_del returns non-zero iff a row existed. */
void st_ban_put(unsigned player_ref, uint64_t id, uint8_t plen,
	unsigned banner);
int st_ban_del(unsigned player_ref, uint64_t id, uint8_t plen);

/* Exact-row lookup with the stored banner: non-zero with *banner set on a
 * hit. The banner is what lets unban re-check authority (banner or current
 * region ruler may lift). */
int st_ban_lookup(unsigned player_ref, uint64_t id, uint8_t plen,
	unsigned *banner);

/* The shared ban refusal: names the banned region and flushes. */
void st_ban_refuse(unsigned player_ref, uint64_t id, uint8_t plen);

/* Name a banned place ("world N", "everywhere", "this region") for ban/unban
 * confirmations and refusals -- one spelling everywhere. */
void st_ban_place(uint64_t id, uint8_t plen, char *buf, size_t len);

/* A command's region selector: argv[world_arg] names a region outright, and
 * without it the caller's default target region -- or failing that their
 * position-derived region -- is returned (st_target_or_position). This is the
 * one parsing dialect for a region selector. */
int st_cmd_region(unsigned player_ref, int argc, char *argv[], int world_arg,
	uint64_t *id, uint8_t *plen);

/* CMD_REGION.md §5.3: parse ONE region selector token -- a bare world number,
 * or the keyword `cosmos`. Split out of st_cmd_region() so `target` accepts the
 * same tokens under the same rules and the same rejections.
 *
 * `cosmos` is (0, ST_PLEN_ROOT). It is a keyword and not `world 0` on purpose:
 * st_planet_id(0) is (0, 16), the first CHILD of the cosmos, and libxylem's own
 * header says "the id alone does not name a region -- (0,0) and (0,16) share
 * it". No numeric token can name the root, so `wall all` had no replacement
 * until this keyword existed. */
int st_cmd_world(const char *tok, uint64_t *id, uint8_t *plen);

/* CMD_REGION.md §4.1-§4.3: the player's default target region, stored in ENT as
 * (target_id, target_plen) with ST_SEL_UNSET as the "no default" sentinel.
 *
 * st_target_get() TRANSLATES the sentinel at the boundary: it returns 1 with a
 * legal plen set, or 0 for "no default", and ST_SEL_UNSET never leaves it. That
 * discipline is not optional -- st_in_scope() reads ST_SEL_UNSET as "every
 * region the actor rules" (the opposite of one chosen region, and a silent
 * authority widening), st_region_covers() is pure arithmetic over the width, and
 * st_can_region() merely happens to reject it because 255 > ST_PLEN_CELL. */
int st_target_get(unsigned player_ref, uint64_t *id, uint8_t *plen);

/* Set or clear the default. Only the setter accepts ST_SEL_UNSET, and only to
 * mean "clear"; every other value must be a legal plen (0..64), which
 * st_cmd_world() and st_region_of_player() already guarantee. */
void st_target_set(unsigned player_ref, uint64_t id, uint8_t plen);
void st_target_clear(unsigned player_ref);

/* The one definition of "which region does this command act on when the player
 * did not say": the player's default target region if they have set one, else
 * the region they are standing in. Every command that takes an optional region
 * selector resolves through this, which is what stops `wall` and `modlist`
 * drifting apart again.
 *
 * Returns XY_OK with a legal plen, or a negative XY_ERR_* with nothing set --
 * the same failure modes as st_region_of_player(), which this falls through to.
 * ST_SEL_UNSET is never returned. */
int st_target_or_position(unsigned player_ref, uint64_t *id, uint8_t *plen);

extern unsigned owner_hd;

#endif
