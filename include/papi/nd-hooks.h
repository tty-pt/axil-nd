#ifndef PAPI_ND_HOOKS_H
#define PAPI_ND_HOOKS_H

/*
 * nd-hooks.h — the NeverDark SIC event surface as libxylem (XY) hooks.
 *
 * These are the game events the ENGINE fires (XY_DEF in the engine provider
 * TU) and GAME MODULES listen to (XY_IMPL in the module). Each is the XY
 * equivalent of an engine SIC_DEF in src/interface.c; the signatures here
 * are the CANONICAL ones (interface.c:464-486) — note uapi/type.h SIC_DECLs
 * drift (e.g. on_del has 2 args here, on_icon carries player_ref).
 *
 * Same XY HOOK SHAPE CONSTRAINT as papi/nd-xy.h: engine XY_DEFs / modules
 * XY_IMPL in a TU that must NOT include this header (or nd-xy.h) for the
 * hooks they define. Modules include THIS header only to CALL an event or to
 * see listener shapes; listeners are written with XY_IMPL in their own TU.
 *
 * Return-value note: the engine's per-module event loop runs every listener
 * in the caller's region; the return value observed by a caller is the last
 * listener's. Struct returns (struct bio / sic_str_t / view_tile_t / struct
 * icon) are legal (<= XY_MAX_RET_SIZE).
 */

#include <stdint.h>

#include <ttypt/xy.h>

#include "papi/nd-xy-types.h"

/* object/view hooks */
XY_DECL(int, on_status, unsigned, player_ref);
XY_DECL(int, on_examine, unsigned, player_ref, unsigned, ref, unsigned, type);
XY_DECL(int, on_add, unsigned, ref, unsigned, type, uint64_t, v);
XY_DECL(unsigned short, on_view_flags, unsigned short, flags, unsigned, ref);
XY_DECL(struct icon, on_icon, unsigned, ref, unsigned, type, unsigned, player_ref);
XY_DECL(int, on_del, unsigned, ref, unsigned, type);
XY_DECL(int, on_clone, unsigned, orig_ref, unsigned, nu_ref);
XY_DECL(int, on_update, unsigned, ref, unsigned, type, double, dt);
XY_DECL(int, on_move, unsigned, ref);

/* editor / tiles */
XY_DECL(int, on_vim, unsigned, ref, sic_str_t, ss);
XY_DECL(struct bio, on_noise, struct bio, bio, uint32_t, he, uint32_t, w,
	uint32_t, tm, uint32_t, cl);
XY_DECL(sic_str_t, on_empty_tile, view_tile_t, t, unsigned, side, sic_str_t, ss);

/* player lifecycle */
XY_DECL(int, on_new_player, unsigned, player_ref);
XY_DECL(int, on_auth, unsigned, player_ref);
XY_DECL(int, on_before_leave, unsigned, ent_ref);
XY_DECL(int, on_leave, unsigned, player_ref, unsigned, loc_ref);
XY_DECL(int, on_enter, unsigned, player_ref, unsigned, loc_ref);
XY_DECL(int, on_after_enter, unsigned, player_ref);
XY_DECL(int, on_spawn, unsigned, player_ref, unsigned, loc_ref, struct bio, bio,
	uint64_t, v);
XY_DECL(int, on_get, unsigned, player_ref, unsigned, ref);

#endif /* PAPI_ND_HOOKS_H */