#ifndef PAPI_ND_XY_H
#define PAPI_ND_XY_H

/*
 * nd-xy.h — the NeverDark game API exposed through libxylem (XY).
 *
 * Replaces the engine's dlopen mod-load / `struct nd` handoff / SIC macros
 * (interface.c mod_load + `*ind = nd`, uapi/type.h SIC_DECL/SIC_DEF/SIC_CALL)
 * with the XY host/module protocol:
 *
 *   - The ENGINE (host) provides every hook below via XY_IMPL in ONE TU that
 *     must NOT include this header (site mods/common/common.h rule). The hook
 *     name with no module override dispatches to the engine's implementation
 *     within the caller's region subtree.
 *   - GAME MODULES include this header freely, then call the hooks as plain C
 *     (XY_DECL forwards through xy_call to the engine provider). Modules
 *     implement their own game events (see papi/nd-hooks.h) with XY_IMPL.
 *   - `get_xy_ptr()` in xy-mod.h supplies the injected xy context
 *     (module_path, region_id, load/unload, ...) — the replacement for
 *     `struct nd *ind = dlsym(sl, "nd"); *ind = nd;`.
 *
 * HOOK SHAPE CONSTRAINT: XY_DECL/XY_DEF emit `ftype ret;` in the inline
 * dispatcher and pack args into a struct literal, so:
 *   - every hook MUST return a scalar type (use `int`, 0 on success / >0 for
 *     fds written, negative XY_ERR_* on failure; `unsigned`/`char *` and
 *     small structs work too) — `void` does not compile;
 *   - every argument MUST be a plain value — `va_list` does not survive the
 *     arg-struct copy. The engine's variadic formatting wrappers
 *     (nd_writef/nd_owritef/nd_twritef, dnotify_wts*) stay engine-side;
 *     modules snprintf into a buffer and call nd_write/nd_rwrite with an
 *     explicit length.
 *   - struct return types must be <= XY_MAX_RET_SIZE (4096 B).
 *   - hooks need >=1 (type, name) arg pair (zero-arg does not compile;
 *     me_get takes a dummy); no array-typed args (pos_t decays — positions
 *     are passed as coord_t *).
 *
 * This header is deliberately SELF-CONTAINED (raw types from
 * papi/nd-xy-types.h). It does NOT include uapi/*.h: those declare the
 * matching fn-pointer globals (e.g. `nd_write_t nd_write;`) which would
 * collide with the XY_DECL inline of the same name in a module TU.
 *
 * Region model (st_* ownership): a game module claims a subspace with
 * `xy_require_claim()` in its xy_install(); `xy_with_region()`/regions scope
 * per-owner plugin dispatch — maps the spacetime per-owner `st_*` loader.
 *
 * This is the XY expansion of `struct nd` (include/papi/nd.h). Members
 * SKIPPED on purpose:
 *   - nd_dwritef / nd_dowritef / nd_tdwritef / dnotify_wts /
 *     dnotify_wts_to — variadic (va_list), engine-side only;
 *   - mod_load / sic_call / sic_areg / sic_get — replaced by xy_load /
 *     xy_call / xy_areg / xy.lib;
 *   - `unsigned hds[HD_MAX]` + `sic_adapter_t *adapter` — data hoisting
 *     moved to hooks/regions (J4.5 #1a);
 *   - `nd_assoc` — inline bring here if a module needs it.
 */

#include <stddef.h>
#include <stdint.h>

#include <ttypt/xy.h>

#include "papi/nd-xy-types.h"

/* ------------------------------------------------------------------ io */

/* fd → player, used to route I/O for a connected socket; NOTHING on miss */
XY_DECL(unsigned, fd_player, unsigned, fd);

/* one player, snprintf'd text with explicit length; #fds written */
XY_DECL(int, nd_write, unsigned, player_ref, char *, str, size_t, len);

/* broadcast to every entity in room_ref except exception_ref */
XY_DECL(int, nd_rwrite, unsigned, room_ref, unsigned, exception_ref,
	char *, str, size_t, len);

/* socket-targeted writes: nd_wwrite → DF_WEBSOCKET fds only,
 * nd_twrites → non-WebSocket (raw telnet) fds only. */
XY_DECL(int, nd_wwrite, unsigned, player_ref, void *, msg, size_t, len);
XY_DECL(int, nd_twrites, unsigned, player_ref, char *, str, size_t, len);

/* close / flush all fds of a player */
XY_DECL(int, nd_close, unsigned, player_ref);
XY_DECL(int, nd_flush, unsigned, player_ref);

/* map-cursor API (hds are engine-side ids): put/get by map id */
XY_DECL(unsigned, nd_put, unsigned, hd, void *, key, void *, data);
XY_DECL(unsigned, nd_get, unsigned, hd, void *, value, void *, key);
XY_DECL(int, nd_open, char *, type, char *, iden, char *, anon, unsigned, flags);
XY_DECL(unsigned, nd_iter, unsigned, hd, void *, key);
XY_DECL(int, nd_next, void *, key, void *, data, unsigned, cur);
XY_DECL(int, nd_fin, unsigned, cur);
XY_DECL(int, nd_len_reg, char *, iden, size_t, len);

/* command registration (command layer) */
typedef void nd_cb_t(int fd, int argc, char *argv[]);
XY_DECL(int, nd_register, char *, str, nd_cb_t *, cb, unsigned, flags);

/* ----------------------------------------------------------------- map */

XY_DECL(int, map_has, unsigned, thing);
XY_DECL(morton_t, map_mwhere, unsigned, thing);
/* XY has no array-typed args (pos_t decays): positions are coord_t * to 4 */
XY_DECL(int, map_where, coord_t *, p, unsigned, thing);
XY_DECL(int, map_delete, unsigned, what);
XY_DECL(unsigned, map_get, coord_t *, p);

/* ------------------------------------------------------------------- st */

XY_DECL(int, st_teleport, unsigned, player_ref, uint64_t, pos);
XY_DECL(int, st_run, unsigned, player_ref, char *, symbol);

/* ------------------------------------------------------------------ wts */

XY_DECL(char *, wts_plural, char *, singular);

/* --------------------------------------------------------------- object */

XY_DECL(int, obj_exists, unsigned, ref);
XY_DECL(unsigned, object_new, OBJ *, obj);
XY_DECL(unsigned, object_copy, OBJ *, nu, unsigned, old_ref);
XY_DECL(int, object_move, unsigned, what_ref, unsigned, where_ref);
XY_DECL(unsigned, object_add, OBJ *, nu, unsigned, skel_id, unsigned, where,
	uint64_t, v);
XY_DECL(int, object_drop, unsigned, where_ref, unsigned, skel_id);
/* engine needs the player for per-player icon dispatch (SESSION-9m). */
XY_DECL(struct icon, object_icon, unsigned, player_ref, unsigned, thing_ref);
XY_DECL(char *, object_art, unsigned, ref);
XY_DECL(const char *, unparse, unsigned, loc_ref);

/* --------------------------------------------------------------- entity */

/* XY needs >=1 arg pair: dummy is ignored, pass 0 */
XY_DECL(unsigned, me_get, unsigned, dummy);
XY_DECL(ENT, ent_get, unsigned, ref);
XY_DECL(int, ent_set, unsigned, ref, ENT *, tmp);
XY_DECL(int, ent_del, unsigned, ref);
XY_DECL(int, ent_reset, ENT *, ent);
XY_DECL(int, controls, unsigned, who_ref, unsigned, what_ref);
XY_DECL(int, payfor, unsigned, who_ref, OBJ *, who, unsigned, cost);
XY_DECL(int, look_around, unsigned, player_ref);
XY_DECL(int, enter, unsigned, player_ref, unsigned, loc_ref, int, e);
XY_DECL(int, look_at, unsigned, player_ref, unsigned, loc_ref);
XY_DECL(unsigned, room_clean, unsigned, loc_ref);

/* --------------------------------------------------------------- ematch */

XY_DECL(unsigned, ematch_at, unsigned, player_ref, unsigned, where_ref, char *, name);
XY_DECL(unsigned, ematch_player, char *, name);
XY_DECL(unsigned, ematch_absolute, char *, name);
XY_DECL(unsigned, ematch_me, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_here, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_mine, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_near, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_all, unsigned, player_ref, char *, name);

/* ------------------------------------------------------ register / vtf */

XY_DECL(unsigned, action_register, char *, label, char *, icon);
XY_DECL(unsigned, vtf_register, char, emp, int, fg, unsigned, flags);

/* ---------------------------------------------------------------- noise */

XY_DECL(struct bio, noise_point, coord_t *, p);

/* ------------------------------------------------------------------ mcp */

XY_DECL(int, fbcp_item, unsigned, player_ref, unsigned, obj_ref, unsigned char, dynflags);
XY_DECL(int, fbcp, unsigned, player_ref, size_t, len, unsigned char, iden, void *, data);
XY_DECL(int, mcp_content_out, unsigned, loc_ref, unsigned, thing_ref);
XY_DECL(int, mcp_content_in, unsigned, loc_ref, unsigned, thing_ref);
XY_DECL(int, mcp_bar, unsigned char, iden, unsigned, player_ref, unsigned short, val, unsigned short, max);

#endif /* PAPI_ND_XY_H */