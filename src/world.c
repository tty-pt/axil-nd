/* world.c — J4.8 step 4: engine boot + world state, split out of
 * x/interface.c.  The bulk of the old main() + the shared struct nd surface
 * live here.  axil is the new ndc: void transport is gone, world boot just
 * opens the store and hands control to the axil module (Phase 4 P3).
 *
 * Owns struct nd, struct nd nd, the persistent world maps, seeds, auth,
 * the command table, and the SI/SIC adapter registration.  The fd registry
 * lives in io.c, the sic/mod machinery in mods.c.
 */

#include "uapi/io.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <arpa/telnet.h>
#include <ttypt/axil.h>
#include <ttypt/axil-tty.h>
#include <ttypt/corm.h>
#include <ttypt/qsys.h>

#include "config.h"
#include "mcp.h"
#include "player.h"
#include "st.h"
#include "uapi/entity.h"
#include "uapi/map.h"
#include "uapi/match.h"
#include "uapi/object.h"
#include "uapi/skel.h"
#include "uapi/type.h"
#include "view.h"

#include "papi/nd.h"

ROO room_zero_room = {
	.flags = RF_HAVEN,
	.doors = 0,
	.exits = 0,
	.floor = 0,
};

struct object room_zero = {
	.location = NOTHING,
	.owner = 1,
	.art_id = 0,
	.type = TYPE_ROOM,
	.value = 9999999,
	.flags = 0,
};

biome_skel_t void_biome_biome = {
	.bg = BLACK,
};

SKEL void_biome = {
	.name = "void",
	.type = TYPE_ROOM,
	.max_art = 1,
};

struct nd nd;

unsigned skel_hd, drop_hd, adrop_hd, element_hd, wts_hd,
	 awts_hd, biome_hd, type_hd, player_hd;
int euid = 0;

void do_avatar(int fd, int argc, char *argv[]);
void do_bio(int fd, int argc, char *argv[]);
void do_ban(int fd, int argc, char *argv[]);
void do_chown(int fd, int argc, char *argv[]);
void do_clone(int fd, int argc, char *argv[]);
void do_connect(int fd, int argc, char *argv[]);
void do_create(int fd, int argc, char *argv[]);
void do_drop(int fd, int argc, char *argv[]);
void do_examine(int fd, int argc, char *argv[]);
void do_get(int fd, int argc, char *argv[]);
void do_inventory(int fd, int argc, char *argv[]);
void do_look_at(int fd, int argc, char *argv[]);
void do_name(int fd, int argc, char *argv[]);
void do_owned(int fd, int argc, char *argv[]);
void do_pose(int fd, int argc, char *argv[]);
void do_recycle(int fd, int argc, char *argv[]);
void do_save(int fd, int argc, char *argv[]);
void do_say(int fd, int argc, char *argv[]);
void do_select(int fd, int argc, char *argv[]);
void do_status(int fd, int argc, char *argv[]);
void do_teleport(int fd, int argc, char *argv[]);
void do_toad(int fd, int argc, char *argv[]);
void do_view(int fd, int argc, char *argv[]);
void do_wall(int fd, int argc, char *argv[]);

static unsigned nd_player_login(int fd, char *user);

void objects_init(void);
void objects_update(double dt);

void map_init(void);
int map_close(unsigned flags);
int map_sync(void);
void mod_close(void);
void mod_load_all(void);
void base_vtf_init(void);

/* The store lives in the axil data root: axil chdir()s into
 * axil_config.chroot before modules load, so a cwd-relative default is also
 * chroot-relative (-C /var/nd -> /var/nd/std.db; plain dev host -> ./std.db,
 * writable). AXIL_ND_DB still overrides. Non-static: map.c + spacetime.c +
 * the nd_open XY provider share this path. */
const char *
world_db(void)
{
	const char *db = getenv("AXIL_ND_DB");

	return db && *db ? db : "std.db";
}

void
close_all(int i)
{
	map_sync();
	corm_save();

	mod_close();

	corm_close(dplayer_hd);
	corm_close(fds_hd);

	st_dlclose();
	corm_close(owner_hd);
	corm_close(sl_hd);

	corm_close(vtf_hd);
	corm_close(situc_hd);
	corm_close(sica_hd);
	corm_close(sican_hd);
	corm_close(bcp_hd);
	corm_close(hd_hd);

	corm_close(action_hd);
	corm_close(type_hd);
	corm_close(ent_hd);
	corm_close(player_hd);
	corm_close(obj_hd);
	corm_close(contents_hd);
	corm_close(obs_hd);
	map_close(0);

	corm_close(skel_hd);
	corm_close(drop_hd);
	corm_close(adrop_hd);
	corm_close(element_hd);
	corm_close(wts_hd);
	corm_close(awts_hd);
	corm_close(biome_hd);

	corm_close(mod_hd);
	corm_close(mod_id_hd);

	closelog();
	if (!i)
		corm_save();
	sync();
	if (i)
		exit(i);
}

/* legacy cursor-API adapters over corm for libnd.a compat (struct nd). */
static unsigned
shared_iter(unsigned hd, void *key)
{
	return corm_iter(hd, key, 0);
}

static int
shared_next(void *key, void *val, unsigned cur)
{
	/* Same zero-byte fix as nd_next: corm_next_copy sizes via corm_len(),
	 * NULL dests skipped, shared_hd stash deleted below. */
	if (!corm_next_copy(key, val, cur)) {
		corm_fin(cur);
		return 0;
	}
	return 1;
}

static void
shared_fin(unsigned cur)
{
	corm_fin(cur);
}

static void
shared_len_reg(char *iden, size_t len)
{
	(void) iden;
	corm_reg(len);
}

static uint32_t
shared_kind(char *t)
{
	if (!strcmp(t, "s"))
		return CM_STR;
	if (!strcmp(t, "p"))
		return CM_PTR;
	return CM_U32;
}

static int
shared_open(char *name, char *kt, char *vt, unsigned flags)
{
	(void) flags;
	corm_open(world_db(), name, shared_kind(kt), shared_kind(vt), 0xFF, 0);
	return 0;
}

/* The engine's `enum hd` -> corm table map (MODS.md §0.2). Defined here
 * because this is where the tables are opened; uapi/io.h declares it and
 * nd_api.c's providers resolve through hd_resolve(). */
unsigned nd_hds[HD_MAX];

/* Tables modules opened with nd_open(), indexed by nd_hd_mod_idx(). The
 * corm handle never leaves this table: a module holds the tag from
 * hd_mod_open() and gets the handle back through hd_resolve(). Fixed cap
 * because the whole module set opens on the order of ten. */
#define HD_MOD_MAX 32
static unsigned hd_mod[HD_MOD_MAX];
static unsigned hd_mod_n;

unsigned
hd_mod_open(char *type, char *iden, char *anon, unsigned flags)
{
	(void) flags;
	if (hd_mod_n >= HD_MOD_MAX)
		return 0;
	unsigned h = corm_open(world_db(), type, shared_kind(iden),
		shared_kind(anon), 0xFF, 0);
	/* corm_open returns 0 on failure; do not burn a registry slot on it. */
	if (!h)
		return 0;
	hd_mod[hd_mod_n] = h;
	return nd_hd_mod(hd_mod_n++);
}

unsigned
hd_resolve(unsigned hd)
{
	if (nd_hd_is_mod(hd)) {
		unsigned idx = nd_hd_mod_idx(hd);
		return idx < hd_mod_n ? hd_mod[idx] : 0;
	}
	return hd < HD_MAX ? nd_hds[hd] : 0;
}

void
shared_init(void)
{
	nd_hds[HD_FD] = fds_hd;
	nd_hds[HD_SKEL] = skel_hd;
	nd_hds[HD_DROP] = drop_hd;
	nd_hds[HD_ADROP] = adrop_hd;
	nd_hds[HD_BIOME] = biome_hd;
	nd_hds[HD_WTS] = wts_hd;
	nd_hds[HD_RWTS] = wts_hd + 2;
	nd_hds[HD_OBJ] = obj_hd;
	nd_hds[HD_OBS] = obs_hd;
	nd_hds[HD_CONTENTS] = contents_hd;
	/* +1/+2 are not arbitrary: the transient type_hd seeds below put
	 * "room" at 1 and "entity" at 2, in that order, before any module
	 * loads. See the comment at those seeds. */
	nd_hds[HD_TYPE] = type_hd;
	nd_hds[HD_RTYPE] = type_hd + 2;
	nd_hds[HD_BCP] = bcp_hd;
	nd_hds[HD_ELEMENT] = element_hd;
	nd_hds[HD_HD] = hd_hd;

	/* The legacy `struct nd` vtable goes away in Phase 3; until then it has
	 * to agree with nd_hds[], so fill it from the same values rather than
	 * repeating the mapping. */
	nd.hds[HD_FD] = fds_hd;
	nd.hds[HD_SKEL] = skel_hd;
	nd.hds[HD_DROP] = drop_hd;
	nd.hds[HD_ADROP] = adrop_hd;
	nd.hds[HD_BIOME] = biome_hd;
	nd.hds[HD_WTS] = wts_hd;
	nd.hds[HD_RWTS] = wts_hd + 2;
	nd.hds[HD_OBJ] = obj_hd;
	nd.hds[HD_OBS] = obs_hd;
	nd.hds[HD_CONTENTS] = contents_hd;
	nd.hds[HD_TYPE] = type_hd;
	nd.hds[HD_RTYPE] = type_hd + 2;
	nd.hds[HD_BCP] = bcp_hd;
	nd.hds[HD_ELEMENT] = element_hd;
	nd.hds[HD_HD] = hd_hd;

	nd.nd_close = eng_nd_close;
	nd.nd_write = eng_nd_write;
	nd.nd_dwritef = nd_dwritef;
	nd.nd_rwrite = eng_nd_rwrite;
	nd.nd_dowritef = nd_dowritef;
	nd.nd_tdwritef = nd_tdwritef;
	nd.nd_wwrite = eng_nd_wwrite;

	nd.fd_player = eng_fd_player;

	nd.map_has = eng_map_has;
	nd.map_mwhere = eng_map_mwhere;
	nd.map_where = eng_map_where;
	nd.map_delete = eng_map_delete;
	nd.map_get = eng_map_get;

	nd.st_teleport = eng_st_teleport;
	nd.st_run = eng_st_run;

	nd.wts_plural = plural;

	nd.obj_exists = eng_obj_exists;
	nd.object_new = eng_object_new;
	nd.object_copy = eng_object_copy;
	nd.object_move = eng_object_move;
	nd.object_add = eng_object_add;
	nd.object_drop = eng_object_drop;

	nd.object_icon = eng_object_icon;
	nd.object_art = eng_object_art;
	nd.unparse = eng_unparse;

	nd.me_get = eng_me_get;
	nd.ent_get = eng_ent_get;
	nd.ent_set = eng_ent_set;
	nd.ent_del = eng_ent_del;
	nd.controls = eng_controls;
	nd.payfor = eng_payfor;
	nd.enter = eng_enter;
	nd.look_at = eng_look_at;
	nd.room_clean = eng_room_clean;

	nd.nd_put = shared_put;
	nd.nd_get = shared_get;
	nd.nd_iter = shared_iter;
	nd.nd_next = shared_next;
	nd.nd_fin = shared_fin;
	nd.nd_len_reg = shared_len_reg;
	nd.nd_open = shared_open;
	nd.nd_register = eng_nd_register;

	nd.ematch_at = eng_ematch_at;
	nd.ematch_player = eng_ematch_player;
	nd.ematch_absolute = eng_ematch_absolute;
	nd.ematch_me = eng_ematch_me;
	nd.ematch_here = eng_ematch_here;
	nd.ematch_mine = eng_ematch_mine;
	nd.ematch_near = eng_ematch_near;
	nd.ematch_all = eng_ematch_all;

	nd.mod_load = mod_load;

	nd.action_register = eng_action_register;
	nd.vtf_register = eng_vtf_register;
	nd.sic_areg = sic_areg;
	nd.sic_get = sic_get;
	nd.sic_call = sic_call;

	nd.noise_point = eng_noise_point;

	nd.fbcp_item = eng_fbcp_item;
	nd.fbcp = eng_fbcp;
	nd.mcp_content_in = eng_mcp_content_in;
	nd.mcp_content_out = eng_mcp_content_out;
	nd.mcp_bar = eng_mcp_bar;
}

int
test_handler(socket_t fd, char *body __attribute__((unused)))
{
	axil_respond(fd, 200, "Test ok\r\n");
	return 0;
}

int
nd_world_init(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	openlog("nd", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);

	unsigned obj_type = corm_reg(sizeof(OBJ));
	unsigned ent_type = corm_reg(sizeof(ENT));
	unsigned skel_type = corm_reg(sizeof(SKEL));
	unsigned drop_type = corm_reg(sizeof(DROP));
	unsigned element_type = corm_reg(sizeof(element_t));
	unsigned biome_type = corm_reg(sizeof(unsigned) * BIOME_MAX);
	unsigned st_type = corm_reg(sizeof(struct st_key));
	unsigned ai_type = corm_reg(sizeof(action_t));
	unsigned pair_type = corm_reg(sizeof(unsigned) * 2);
	unsigned vtf_type = corm_reg(sizeof(vtf_t));
	unsigned sica_type = corm_reg(sizeof(sic_adapter_t));

	const char *db = world_db();

	nd_io_init();
	signal(SIGSEGV, close_all);

	owner_hd = corm_open(db, "st", st_type, CM_U32, 0xFFFF, 0);
	sl_hd = corm_open(db, "sl", st_type, CM_PTR, 0xFFFF, 0);

	vtf_hd = corm_open(NULL, "vtf", CM_U32, vtf_type, 0xFF, CM_AINDEX);
	situc_hd = corm_open(NULL, NULL, pair_type, CM_PTR, 0xFF, 0);
	sica_hd = corm_open(NULL, "sica", CM_U32, sica_type, 0xFF, CM_AINDEX);
	sican_hd = corm_open(NULL, NULL, CM_STR, CM_U32, 0xFF, 0);
	bcp_hd = corm_open(NULL, "bcp", CM_U32, CM_STR, 0xFF, CM_AINDEX);
	hd_hd = corm_open(NULL, NULL, CM_STR, CM_U32, 0xFF, 0);

	/* action_hd/type_hd are boot-registration maps, not state: modules (and
	 * the host) register during load, and every consumer either iterates
	 * the whole map (mcp_actions) or is handed the offsets below fresh each
	 * boot. Persisting them only let a per-boot re-registration append
	 * duplicates — the host seeds are guarded by !existed, but
	 * mod_load_all() re-runs every module's xy_install on every later boot.
	 * Same family as vtf_hd/sica_hd/bcp_hd just above: transient, and
	 * repopulated unconditionally below. */
	action_hd = corm_open(NULL, "action", CM_U32, ai_type, 0xFFFF, CM_AINDEX);
	type_hd = corm_open(NULL, "ndt", CM_U32, CM_STR, 0xFFFF, CM_AINDEX);
	ent_hd = corm_open(db, "entity", CM_U32, ent_type, 0xFFFF, 0);
	player_hd = corm_open(db, "player", CM_STR, CM_U32, 0xFF, 0);
	obj_hd = corm_open(db, "obj", CM_U32, obj_type, 0xFFFF, CM_AINDEX);
	/* contents/obs are DERIVED indices over persisted sources, not state
	 * in their own right, so they stay transient: contents is a pure
	 * function of obj_hd (one pair per object with location != NOTHING),
	 * and obs is rebuilt by eng_look_at as players move. objects_init
	 * runs unconditionally below and rebuilds contents from obj_hd
	 * before any read, so persisting them could only duplicate pairs and
	 * strand dead-session observers. The persisted state is obj_hd plus
	 * ent_hd.last_observed, which is what survives a reboot. */
	contents_hd = corm_open(NULL, "contents", CM_U32, CM_U32, 0xFFFF,
				CM_SORTED | CM_MULTIVALUE);
	obs_hd = corm_open(NULL, "obs", CM_U32, CM_U32, 0xFFFF,
			   CM_SORTED | CM_MULTIVALUE);
	map_init();

	skel_hd = corm_open(db, "skel", CM_U32, skel_type, 0xFFFF, CM_AINDEX);
	drop_hd = corm_open(db, "drop", CM_U32, drop_type, 0xFFFF, CM_AINDEX);
	adrop_hd = corm_open(db, "adrop", CM_U32, CM_U32, 0xFFFF,
			     CM_SORTED | CM_MULTIVALUE);
	element_hd = corm_open(db, "element", CM_U32, element_type, 0xFFFF,
			       CM_AINDEX);
	wts_hd = corm_open(db, "wts", CM_U32, CM_STR, 0xFFFF, CM_AINDEX);
	awts_hd = corm_open(db, "awts", CM_U32, CM_U32, 0xFFFF,
			    CM_SORTED | CM_MULTIVALUE);
	biome_hd = corm_open(db, "biome", CM_U32, biome_type, 0xFFFF, 0);

	shared_init();

	mod_id_hd = corm_open(db, "module_id", CM_U32, CM_PTR, 0xFF, CM_AINDEX);
	mod_hd = corm_open(NULL, "mod", CM_STR, CM_U32, 0xFF, 0);

	/* obj_hd persists, so room 0 distinguishes fresh from existing. Decided
	 * once here, before any seeding puts below; every !existed guard in
	 * this function reads it. */
	unsigned zero = 0;
	unsigned existed = (corm_get(obj_hd, &zero) != NULL);
	if (!existed) {
		memcpy(room_zero.data, &room_zero_room, sizeof(room_zero_room));
		corm_put(obj_hd, NULL, &room_zero);
	}

	/* The 20 SIC_AREG() calls that used to stand here are gone. SIC_AREG
	 * was world.c-local (`fname##_id = sic_areg(XSTR(fname), &fname##_sic_adapter)`)
	 * and registered every adapter explicitly at boot, redundantly with the
	 * .sic_auto_init sections the SIC_DEFs emitted. Registration is now a
	 * single path: each XY_DEF's AUTO_INIT constructor in src/nd_events.c
	 * (folded into libaxil-nd.c) registers its own hook. */

	/* type_hd is transient: these two are its only entries, re-registered
	 * every boot in this fixed order. The order is load-bearing, not
	 * incidental — object.c:186 reads type_hd + 1 and world.c sets
	 * nd.hds[HD_RTYPE] = type_hd + 2, both assuming "room" is id 1 and
	 * "entity" is id 2. These seeds run before mod_load_all()/mod_init
	 * below, so the host claims those ids first on every boot. If a
	 * module-facing type registration API is ever added, whoever seeds
	 * here still has to go first, or those two offsets must become
	 * by-name lookups. */
	corm_put(type_hd, NULL, "room");
	corm_put(type_hd, NULL, "entity");

	corm_put(bcp_hd, NULL, "item");
	corm_put(bcp_hd, NULL, "view");
	corm_put(bcp_hd, NULL, "view_buffer");
	corm_put(bcp_hd, NULL, "room");
	corm_put(bcp_hd, NULL, "entity");
	corm_put(bcp_hd, NULL, "auth_failure");
	corm_put(bcp_hd, NULL, "auth_success");
	corm_put(bcp_hd, NULL, "out");
	corm_put(bcp_hd, NULL, "tod");
	corm_put(bcp_hd, NULL, "action");

	/* action_hd is transient, so these are re-registered every boot —
	 * unconditionally, like the bcp_hd puts above. They must precede
	 * mod_load_all()/mod_init below so the built-in actions keep the low
	 * ids; module-registered actions append after. */
	eng_action_register("look", "\xf0\x9f\x94\x8d");
	eng_action_register("get", "\xf0\x9f\x96\x90\xef\xb8\x8f");
	eng_action_register("drop", "\xf0\x9f\xaa\xa3");

	base_vtf_init();

	if (!existed) {
		element_t spirit = {
			.color = MAGENTA,
			.weakness = ELM_SPIRIT,
		}, fire = {
			.color = RED,
			.weakness = ELM_WATER,
		}, water = {
			.color = BLUE,
			.weakness = ELM_FIRE,
		}, air = {
			.color = WHITE,
			.weakness = ELM_EARTH,
		}, earth = {
			.color = YELLOW,
			.weakness = ELM_AIR,
		}, physical = {
			.color = GREEN,
			.weakness = ELM_SPIRIT,
		};

		corm_put(element_hd, NULL, &spirit);
		corm_put(element_hd, NULL, &fire);
		corm_put(element_hd, NULL, &water);
		corm_put(element_hd, NULL, &air);
		corm_put(element_hd, NULL, &earth);
		corm_put(element_hd, NULL, &physical); /* 5 */

		SENT adam_sent = {
			.y = 255,
		};

		SKEL adam = {
			.name = "human",
			.type = TYPE_ENTITY,
			.max_art = 300,
		};

		memcpy(adam.data, &adam_sent, sizeof(adam_sent));
		corm_put(skel_hd, NULL, &adam);     /* 0 */
		corm_put(wts_hd, NULL, "punch");    /* 0 */

		unsigned biome_map[BIOME_MAX];
		memcpy(void_biome.data, &void_biome_biome, sizeof(void_biome_biome));
		unsigned void_ref = corm_put(skel_hd, NULL, &void_biome);
		for (unsigned i = 0; i < BIOME_MAX; i++)
			biome_map[i] = void_ref;
		void_ref = 16;
		corm_put(biome_hd, &void_ref, biome_map);

		unsigned owner = 1;
		sthd_put(owner_hd, 0, 0, &owner);
	}

	objects_init();

	if (existed)
		mod_load_all();
	else {
		st_put(1, 0, 64);
		eng_st_run(-1, "mod_init");
	}

	srand(getpid());

	setenv("TERM", "xterm-256color", 1);
	st_init();

	axil_register_handler("/test", &test_handler);

	WARN("Done.\n");

	return 0;
}

void
do_sh(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	axil_tty_shell(fd);
}

void
do_man(int fd, int argc, char *argv[])
{
	const char *topic = (argc > 1 && argv[1] && *argv[1]) ? argv[1] : "begin";
	char path[BUFSIZ];

	snprintf(path, sizeof(path), "man/%s.10", topic);

	if (access(path, R_OK) == 0) {
		char *rargv[] = { "/usr/bin/man", "-P", "cat", "-l", path, NULL };
		axil_tty_exec(fd, rargv);
	} else {
		char *rargv[] = { "/usr/bin/man", "-P", "cat", "-s", "10", (char *)topic, NULL };
		axil_tty_exec(fd, rargv);
	}
}

struct cmd_slot cmds[] = {
	{
		.name = "sh",
		.cb = &do_sh,
	}, {
		.name = "GET",
		.cb = &do_GET,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "POST",
		.cb = &do_POST,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "PRI",
		.cb = &do_GET,
		.flags = CF_NOAUTH | CF_NOTRIM,
	}, {
		.name = "connect",
		.cb = &do_connect,
		.flags = CF_NOAUTH,
	}, {
		.name = "avatar",
		.cb = &do_avatar,
	}, {
		.name = "bio",
		.cb = &do_bio,
	}, {
		.name = "ban",
		.cb = &do_ban,
	}, {
		.name = "chown",
		.cb = &do_chown,
	}, {
		.name = "clone",
		.cb = &do_clone,
	}, {
		.name = "create",
		.cb = &do_create,
	}, {
		.name = "name",
		.cb = &do_name,
	}, {
		.name = "owned",
		.cb = &do_owned,
	}, {
		.name = "recycle",
		.cb = &do_recycle,
	}, {
		.name = "teleport",
		.cb = &do_teleport,
	}, {
		.name = "wall",
		.cb = &do_wall,
	}, {
		.name = "drop",
		.cb = &do_drop,
	}, {
		.name = "examine",
		.cb = &do_examine,
	}, {
		.name = "get",
		.cb = &do_get,
	}, {
		.name = "inventory",
		.cb = &do_inventory,
	}, {
		.name = "look",
		.cb = &do_look_at,
	}, {
		.name = "view",
		.cb = &do_view,
	}, {
		.name = "man",
		.cb = &do_man,
	}, {
		.name = "help",
		.cb = &do_man,
	}, {
		.name = "pose",
		.cb = &do_pose,
	}, {
		.name = "say",
		.cb = &do_say,
	}, {
		.name = "save",
		.cb = &do_save,
	}, {
		.name = "select",
		.cb = &do_select,
	}, {
		.name = "stchown",
		.cb = &do_stchown,
	}, {
		.name = "streload",
		.cb = &do_streload,
	}, {
		.name = "status",
		.cb = &do_status,
	}, {
		.name = NULL,
		.cb = NULL,
	},
};

/* Expose the nd command table to axil's dispatcher.  GET/POST/PRI are axil
 * core HTTP commands (do_GET/do_POST in libaxil.so) — registering them here
 * would shadow axil's request_handle routing, so they stay reserved. */
void
nd_register_commands(void)
{
	for (size_t i = 0; cmds[i].cb; i++) {
		if (strcmp(cmds[i].name, "GET") == 0 ||
		    strcmp(cmds[i].name, "POST") == 0 ||
		    strcmp(cmds[i].name, "PRI") == 0)
			continue;

		axil_register(cmds[i].name, cmds[i].cb, cmds[i].flags);
	}
}

void
do_save(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd);

	if (player_ref != 1) {
		nd_writef(player_ref, "Only root can save\n");
		return;
	}

	map_sync();
	corm_save();
}

static inline void
avatar(OBJ *player)
{
	const SKEL *skel = corm_get(skel_hd, &player->skid);

	player->art_id = 1 + (random() % (skel && skel->max_art ? skel->max_art : 1));
}

void
do_avatar(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd);
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &player);
	avatar(&player);
	corm_put(obj_hd, &player_ref, &player);
	eng_mcp_content_out(player.location, player_ref);
	eng_mcp_content_in(player.location, player_ref);
}

static inline void mcp_actions(unsigned player_ref) {
	unsigned c = corm_iter(action_hd, NULL, 0);
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		const action_t *ai = vp;

		mcp_action(player_ref, *(const unsigned *)kp, (char *)ai->label,
			   (char *)ai->icon);
	}
	corm_fin(c);
}

unsigned
auth(unsigned fd)
{
	/* Identity source: REMOTE_USER, set by axil_auth() (WS-phase handler
	 * or -A auto-auth).  cmd_proc's auth gate (libaxil.c) already dropped
	 * non-CF_NOAUTH commands pre-auth, so raw guests only boot via
	 * do_connect — the dead QSESSION branch is gone. */
	char env_user[BUFSIZ];

	if (axil_env_get(fd, env_user, sizeof(env_user), "REMOTE_USER") == 0 &&
	    *env_user)
		return nd_player_login(fd, env_user);

	mcp_auth_fail(fd, 3);
	return 0;
}

/* Re-anchor a player to a new socket on (re)login: writes must go to the
 * socket the player is logging in on, not to a descriptor that may already
 * be gone.  Traced with gdb: a raw guest re-running `connect <name>` for a
 * name already held by a WebSocket player left the old fd in the write set,
 * eng_nd_wwrite() sent on stale fd 5, and the process died.  nd_io_attach()
 * (io.c) replaces rather than appends, so a relogin re-points the player. */
void nd_io_attach(unsigned fd, unsigned player_ref);

/* Shared create/re-login + landing for WS (auth via REMOTE_USER) and raw
 * guests (do_connect).  axil_auth() here is what flips DF_AUTHENTICATED so
 * subsequent commands pass cmd_proc's auth gate. */
static unsigned
nd_player_login(int fd, char *user)
{
	OBJ player;
	unsigned player_ref = player_get(user);

	WARN("'%s' (%u/%u)\n", user, fd, player_ref);

	if (player_ref == NOTHING) {
		player_ref = eng_object_add(&player, 0, NOTHING, 0, OF_PLAYER);
		strlcpy(player.name, user, sizeof(player.name));
		player.value = 150;
		player_put(user, player_ref);
		avatar(&player);

		nd_io_attach(fd, player_ref);
		corm_put(obj_hd, &player_ref, &player);

		nd_evt_new_player(player_ref);
	} else {
		ENT eplayer = eng_ent_get(player_ref);

		if (eplayer.flags & EF_BAN) {
			nd_writef(player_ref, "You are banned.\n");
			return 0;
		}

		nd_io_attach(fd, player_ref);
	}

	if (axil_auth(fd, user))
		return NOTHING;

	mcp_auth_success(player_ref);
	mcp_actions(player_ref);
	eng_look_at(player_ref, NOTHING);
	do_view(fd, 0, NULL);
	if (day_n)
		mcp_tod(player_ref, 1);
	else
		mcp_tod(player_ref, 0);

	nd_evt_auth(player_ref);
	return player_ref;
}

void nd_event_announce(unsigned player_ref, unsigned loc_ref);

void
do_connect(int fd, int argc, char *argv[])
{
	if (argc < 2) {
		axil_write(fd, "Usage: connect <name>\r\n", strlen("Usage: connect <name>\r\n"));
		return;
	}

	unsigned player_ref = nd_player_login(fd, argv[1]);

	if (player_ref && player_ref != NOTHING) {
		nd_io_attach(fd, player_ref);
		nd_event_announce(player_ref, player_ref);
		eng_nd_flush(player_ref);
	}
}

int
nd_connect(int fd)
{
	return auth(fd);
}

void
nd_disconnect(int fd)
{
	if (!(axil_flags(fd) & DF_AUTHENTICATED))
		return;

	unsigned player_ref = eng_fd_player(fd);
	const OBJ *player = corm_get(obj_hd, &player_ref);

	WARN("%s(%u) on fd %d\n", player ? player->name : "?",
	     player_ref, fd);
	nd_io_detach(fd);
}

void
nd_update(unsigned long long dt)
{
	/* dt is microseconds, so fdt is real seconds and the gate below fires
	 * once per second. mul is day ticks per update: 0.5 stretches the
	 * 1 << DAYTICK_Y (1024) cycle to 2048 s, ~34 min, sunset at ~17 min. */
	double mul = 0.5;
	double fdt = dt / 1000000.0;

	tick += fdt;
	if (tick > 1.0) {
		tick -= 1.0;
		objects_update(1.0 * mul);
		st_update(1.0 * mul);
	}
}

void
nd_vim(int fd, int argc __attribute__((unused)), char *argv[]) {
	if (!(axil_flags(fd) & DF_AUTHENTICATED))
		return;

	if (!argv || !argv[0] || !*argv[0])
		return;

	unsigned player_ref = eng_fd_player(fd);
	if (!player_ref)
		return;

	char const *s = argv[0];
	unsigned pos = 0;
	sic_str_t ss = { .str = "", .pos = 0 };

	strlcpy(ss.str, argv[0], sizeof(ss.str));

	for (; s[pos]; ) {
		unsigned old_pos = pos;
		int ret = st_v(player_ref, s + pos);

		pos += ret < 0 ? - ret : ret;
		ss.pos = pos;
		ret = nd_evt_vim(player_ref, ss);
		pos += ret < 0 ? - ret : ret;

		if (pos == old_pos && s[pos])
			pos++;
	}
}

void
nd_command(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	me = eng_fd_player(fd);
	nd_io_reset(fd);
}

unsigned *biome_map_get(uint64_t position) {
	static unsigned biome_map[BIOME_MAX];
	const void *v;
	unsigned ref;

	for (int i = 0; i < 16; i++) {
		ref = ((position >> (48 + i)) << 4) | i;
		v = corm_get(biome_hd, &ref);
		if (v) {
			memcpy(biome_map, v, sizeof(biome_map));
			return biome_map;
		}
	}

	ref = 16;
	v = corm_get(biome_hd, &ref);
	if (v) {
		memcpy(biome_map, v, sizeof(biome_map));
		return biome_map;
	}

	return void_biome.data;
}
