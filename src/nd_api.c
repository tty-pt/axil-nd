/* nd_api.c — engine XY_IMPL side for the nd/xy.h service hooks.
 *
 * Folded into src/libaxil-nd.c (single-TU module): do NOT compile standalone
 * and do NOT include nd/xy.h here (XY_IMPL + XY_DECL clash). Value types
 * come from nd/xy-types.h only; uapi headers are NOT includable here
 * (their enum/struct defs collide with nd/xy-types.h), so the engine
 * providers are forward-declared locally below.
 *
 * PROVIDER POLICY (post-J4.8-step-4 flip): every hook below calls the real
 * engine TU function (renamed `eng_*` so the C symbol differs from the hook
 * name). Hook signatures that differ from the engine's are adapted inline:
 * `coord_t *` args pass straight through to `pos_t` params (ABI-identical),
 * `void`-returning engine fns return a 0 success, `object_add` passes
 * `flags=0`, `nd_register` returns 0. `ent_reset`/`look_around` have no
 * engine body anywhere and stay documented no-ops (user decision).
 * The io hooks (fd_player/nd_write/...) live in nd_xy.c.
 */

#include <stddef.h>
#include <string.h>

#include <ttypt/corm.h>

#include "nd/xy-types.h"

typedef void nd_cb_t(int fd, int argc, char *argv[]);

/* engine providers (engine TUs) */
int eng_map_has(unsigned thing);
morton_t eng_map_mwhere(unsigned thing);
void eng_map_where(coord_t *p, unsigned thing);
int eng_map_delete(unsigned what);
unsigned eng_map_get(coord_t *p);
void eng_st_teleport(unsigned player_ref, uint64_t pos);
void eng_st_run(unsigned player_ref, char *symbol);
char *plural(char *singular);
int eng_obj_exists(unsigned ref);
unsigned eng_object_new(OBJ *obj);
unsigned eng_object_copy(OBJ *nu, unsigned old_ref);
void eng_object_move(unsigned what_ref, unsigned where_ref);
unsigned eng_object_add(OBJ *nu, unsigned skel_id, unsigned where,
	uint64_t v, unsigned flags);
void eng_object_drop(unsigned where_ref, unsigned skel_id);
struct icon eng_object_icon(unsigned player_ref, unsigned thing_ref);
char *eng_object_art(unsigned ref);
const char *eng_unparse(unsigned loc_ref);
unsigned eng_me_get(void);
ENT eng_ent_get(unsigned ref);
void eng_ent_set(unsigned ref, ENT *tmp);
void eng_ent_del(unsigned ref);
int eng_controls(unsigned who_ref, unsigned what_ref);
int eng_payfor(unsigned who_ref, OBJ *who, unsigned cost);
void eng_enter(unsigned player_ref, unsigned loc_ref, enum exit e);
void eng_look_at(unsigned player_ref, unsigned loc_ref);
unsigned eng_room_clean(unsigned loc_ref);
unsigned eng_ematch_at(unsigned player_ref, unsigned where_ref, char *name);
unsigned eng_ematch_player(char *name);
unsigned eng_ematch_absolute(char *name);
unsigned eng_ematch_me(unsigned player_ref, char *str);
unsigned eng_ematch_here(unsigned player_ref, char *str);
unsigned eng_ematch_mine(unsigned player_ref, char *str);
unsigned eng_ematch_near(unsigned player_ref, char *str);
unsigned eng_ematch_all(unsigned player_ref, char *name);
unsigned eng_action_register(char *label, char *icon);
unsigned eng_vtf_register(char emp, enum color fg, unsigned flags);
struct bio eng_noise_point(coord_t *p);
void eng_fbcp_item(unsigned player_ref, unsigned obj_ref,
	unsigned char dynflags);
void eng_fbcp(unsigned player_ref, size_t len, unsigned char iden,
	void *data);
void eng_mcp_content_out(unsigned loc_ref, unsigned thing_ref);
void eng_mcp_content_in(unsigned loc_ref, unsigned thing_ref);
void eng_mcp_bar(unsigned char iden, unsigned player_ref,
	unsigned short val, unsigned short max);
unsigned shared_put(unsigned hd, void *key, void *data);
unsigned shared_get(unsigned hd, void *value, void *key);
/* MODS.md §0.2 handle resolution: nd_get/nd_put/nd_iter take a module-facing
 * handle (an `enum hd`, or an nd_open() tag) and the engine maps it to a corm
 * table. uapi/io.h declares these, but nd_api.c cannot include uapi headers
 * (their struct/enum defs collide with nd/xy-types.h), so they are
 * forward-declared here. HD_MAX itself comes from nd/hd.h, which
 * nd/xy-types.h includes. */
unsigned hd_resolve(unsigned hd);
unsigned hd_mod_open(char *type, char *iden, char *anon, unsigned flags);
void eng_nd_register(char *str, nd_cb_t *cb, unsigned flags);
/* engine body for the nd_assoc hook (renamed per the PROVIDER POLICY above);
 * its type comes from nd/xy-types.h, since uapi/io.h is not includable here. */
void eng_nd_assoc(unsigned hd, unsigned link, nd_assoc_cb_t assoc);
const char *world_db(void);

/* map-cursor API (real corm bodies honoring the XY signatures) */
XY_IMPL(unsigned, nd_put, unsigned, hd, void *, key, void *, data)
{
	return shared_put(hd_resolve(hd), key, data);
}

XY_IMPL(unsigned, nd_get, unsigned, hd, void *, value, void *, key)
{
	return shared_get(hd_resolve(hd), value, key);
}

XY_IMPL(int, nd_open, char *, type, char *, iden, char *, anon, unsigned, flags)
{
	/* Returns a TAGGED module handle (nd/hd.h), not a corm handle:
	 * a module's own table and the engine's `enum hd` share one unsigned,
	 * and a bare corm handle could collide with e.g. HD_OBJ == 7. This used
	 * to discard corm_open's result and return 0, so every module that
	 * saved the handle (nd-class, nd-level, nd-attr) got 0 and then read
	 * and wrote table 0. */
	return (int)hd_mod_open(type, iden, anon, flags);
}

XY_IMPL(unsigned, nd_iter, unsigned, hd, void *, key)
{
	return corm_iter(hd_resolve(hd), key, 0);
}

XY_IMPL(int, nd_next, void *, key, void *, data, unsigned, cur)
{
	/* corm_next_copy sizes via corm_len(), not corm_type_len(): the old
	 * memcpy pair copied ZERO bytes for measured CM_STR keys/values
	 * (corm_mreg sets len 0). NULL dests are skipped by the primitive,
	 * so the key/data guards and the nd_cur_hd stash are gone. */
	if (!corm_next_copy(key, data, cur)) {
		corm_fin(cur);
		return 0;
	}
	return 1;
}

XY_IMPL(int, nd_fin, unsigned, cur)
{
	corm_fin(cur);
	return 0;
}

XY_IMPL(int, nd_len_reg, char *, iden, size_t, len)
{
	/* Register the (name, type) mapping so hd_mod_open() opens tables with
	 * correctly-sized value types (world.c). corm_reg() alone discards the
	 * name, which used to truncate every struct value to 4 bytes. */
	extern void mod_type_register(char *name, unsigned tid);
	unsigned tid = corm_reg(len);
	mod_type_register(iden, tid);
	return (int)tid;
}

/* nd_assoc(hd, link, cb): index the primary table `link` by keys `cb` derives
 * from each row, writing them into the secondary table `hd`. nd-race's only
 * use: name -> race_id, via race_rhd (secondary) linked to race_hd (primary).
 *
 * Both handles arrive as module-facing values -- nd-race passes two nd_open()
 * tags -- so both go through hd_resolve(), matching nd_put/nd_get/nd_iter.
 * The raw corm handles would otherwise be passed straight to corm_assoc(),
 * indexing whatever table the tag happened to collide with.
 *
 * int, not void: XY_IMPL cannot express a void return (sizeof(ftype) and
 * `ftype result = ...` are both ill-formed for void). nd-race ignores it. */
XY_IMPL(int, nd_assoc, unsigned, hd, unsigned, link, nd_assoc_cb_p, assoc)
{
	eng_nd_assoc(hd_resolve(hd), hd_resolve(link), assoc);
	return 0;
}

XY_IMPL(int, nd_register, char *, str, nd_cb_t *, cb, unsigned, flags)
{
	eng_nd_register(str, cb, flags);
	return 0;
}

/* map (real impl in map.c) */
XY_IMPL(int, map_has, unsigned, thing)
{
	return eng_map_has(thing);
}

XY_IMPL(morton_t, map_mwhere, unsigned, thing)
{
	return eng_map_mwhere(thing);
}

XY_IMPL(int, map_where, coord_t *, p, unsigned, thing)
{
	eng_map_where(p, thing);
	return 0;
}

XY_IMPL(int, map_delete, unsigned, what)
{
	return eng_map_delete(what);
}

XY_IMPL(unsigned, map_get, coord_t *, p)
{
	return eng_map_get(p);
}

/* st (real impl in spacetime.c) */
XY_IMPL(int, st_teleport, unsigned, player_ref, uint64_t, pos)
{
	eng_st_teleport(player_ref, pos);
	return 0;
}

XY_IMPL(int, st_run, unsigned, player_ref, char *, symbol)
{
	eng_st_run(player_ref, symbol);
	return 0;
}

/* wts (real impl: plural() in io.c) */
XY_IMPL(char *, wts_plural, char *, singular)
{
	return plural(singular);
}

/* object (real impl in object.c) */
XY_IMPL(int, obj_exists, unsigned, ref)
{
	return eng_obj_exists(ref);
}

XY_IMPL(unsigned, object_new, OBJ *, obj)
{
	return eng_object_new(obj);
}

XY_IMPL(unsigned, object_copy, OBJ *, nu, unsigned, old_ref)
{
	return eng_object_copy(nu, old_ref);
}

XY_IMPL(int, object_move, unsigned, what_ref, unsigned, where_ref)
{
	eng_object_move(what_ref, where_ref);
	return 0;
}

XY_IMPL(unsigned, object_add, OBJ *, nu, unsigned, skel_id, unsigned, where,
	uint64_t, v)
{
	/* engine takes a creation-flags tail arg the hook doesn't carry;
	 * modules create plain objects (flags=0). */
	return eng_object_add(nu, skel_id, where, v, 0);
}

XY_IMPL(int, object_drop, unsigned, where_ref, unsigned, skel_id)
{
	eng_object_drop(where_ref, skel_id);
	return 0;
}

XY_IMPL(struct icon, object_icon, unsigned, player_ref, unsigned, thing_ref)
{
	return eng_object_icon(player_ref, thing_ref);
}

XY_IMPL(char *, object_art, unsigned, ref)
{
	return eng_object_art(ref);
}

XY_IMPL(const char *, unparse, unsigned, loc_ref)
{
	return eng_unparse(loc_ref);
}

/* entity (real impl in entity.c) */
XY_IMPL(unsigned, me_get, unsigned, dummy)
{
	(void)dummy;
	return eng_me_get();
}

XY_IMPL(ENT, ent_get, unsigned, ref)
{
	return eng_ent_get(ref);
}

XY_IMPL(int, ent_set, unsigned, ref, ENT *, tmp)
{
	eng_ent_set(ref, tmp);
	return 0;
}

XY_IMPL(int, ent_del, unsigned, ref)
{
	eng_ent_del(ref);
	return 0;
}

XY_IMPL(int, ent_reset, ENT *, ent)
{
	/* no engine body exists; documented no-op (user decision). */
	(void)ent;
	return 0;
}

XY_IMPL(int, controls, unsigned, who_ref, unsigned, what_ref)
{
	return eng_controls(who_ref, what_ref);
}

XY_IMPL(int, payfor, unsigned, who_ref, OBJ *, who, unsigned, cost)
{
	return eng_payfor(who_ref, who, cost);
}

XY_IMPL(int, look_around, unsigned, player_ref)
{
	/* no engine body exists; documented no-op (user decision). */
	(void)player_ref;
	return 0;
}

XY_IMPL(int, enter, unsigned, player_ref, unsigned, loc_ref, int, e)
{
	eng_enter(player_ref, loc_ref, (enum exit)e);
	return 0;
}

XY_IMPL(int, look_at, unsigned, player_ref, unsigned, loc_ref)
{
	eng_look_at(player_ref, loc_ref);
	return 0;
}

XY_IMPL(unsigned, room_clean, unsigned, loc_ref)
{
	return eng_room_clean(loc_ref);
}

/* ematch (real impl in match.c) */
XY_IMPL(unsigned, ematch_at, unsigned, player_ref, unsigned, where_ref, char *, name)
{
	return eng_ematch_at(player_ref, where_ref, name);
}

XY_IMPL(unsigned, ematch_player, char *, name)
{
	return eng_ematch_player(name);
}

XY_IMPL(unsigned, ematch_absolute, char *, name)
{
	return eng_ematch_absolute(name);
}

XY_IMPL(unsigned, ematch_me, unsigned, player_ref, char *, str)
{
	return eng_ematch_me(player_ref, str);
}

XY_IMPL(unsigned, ematch_here, unsigned, player_ref, char *, str)
{
	return eng_ematch_here(player_ref, str);
}

XY_IMPL(unsigned, ematch_mine, unsigned, player_ref, char *, str)
{
	return eng_ematch_mine(player_ref, str);
}

XY_IMPL(unsigned, ematch_near, unsigned, player_ref, char *, str)
{
	return eng_ematch_near(player_ref, str);
}

XY_IMPL(unsigned, ematch_all, unsigned, player_ref, char *, name)
{
	return eng_ematch_all(player_ref, name);
}

/* register / vtf (real impl in mods.c, over corm maps per J4.5 #3) */
XY_IMPL(unsigned, action_register, char *, label, char *, icon)
{
	return eng_action_register(label, icon);
}

XY_IMPL(unsigned, vtf_register, char, emp, int, fg, unsigned, flags)
{
	return eng_vtf_register(emp, fg, flags);
}

/* noise (real impl in noise.c) */
XY_IMPL(struct bio, noise_point, coord_t *, p)
{
	return eng_noise_point(p);
}

/* mcp (real impl in mcp.c) */
XY_IMPL(int, fbcp_item, unsigned, player_ref, unsigned, obj_ref, unsigned char, dynflags)
{
	eng_fbcp_item(player_ref, obj_ref, dynflags);
	return 0;
}

XY_IMPL(int, fbcp, unsigned, player_ref, size_t, len, unsigned char, iden, void *, data)
{
	eng_fbcp(player_ref, len, iden, data);
	return 0;
}

XY_IMPL(int, mcp_content_out, unsigned, loc_ref, unsigned, thing_ref)
{
	eng_mcp_content_out(loc_ref, thing_ref);
	return 0;
}

XY_IMPL(int, mcp_content_in, unsigned, loc_ref, unsigned, thing_ref)
{
	eng_mcp_content_in(loc_ref, thing_ref);
	return 0;
}

XY_IMPL(int, mcp_bar, unsigned char, iden, unsigned, player_ref, unsigned short, val, unsigned short, max)
{
	eng_mcp_bar(iden, player_ref, val, max);
	return 0;
}
