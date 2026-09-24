/* nd_events.c — engine XY_DEF side for the papi/nd-hooks.h SIC events.
 *
 * Each XY_DEF emits the canonical adapter + an inline dispatcher the engine
 * calls to fire the event; game modules XY_IMPL the listeners they want.
 * Folded into src/libaxil-nd.c (single-TU module): do NOT compile standalone
 * and do NOT include papi/nd-hooks.h here (XY_DEF + XY_DECL clash).
 */

#include "papi/nd-xy-types.h"

XY_DEF(int, on_status, unsigned, player_ref);
XY_DEF(int, on_examine, unsigned, player_ref, unsigned, ref, unsigned, type);
XY_DEF(int, on_add, unsigned, ref, unsigned, type, uint64_t, v);
XY_DEF(unsigned short, on_view_flags, unsigned short, flags, unsigned, ref);
XY_DEF(struct icon, on_icon, unsigned, ref, unsigned, type, unsigned, player_ref);
XY_DEF(int, on_del, unsigned, ref, unsigned, type);
XY_DEF(int, on_clone, unsigned, orig_ref, unsigned, nu_ref);
XY_DEF(int, on_update, unsigned, ref, unsigned, type, double, dt);
XY_DEF(int, on_move, unsigned, ref);

XY_DEF(int, on_vim, unsigned, ref, sic_str_t, ss);
XY_DEF(struct bio, on_noise, struct bio, bio, uint32_t, he, uint32_t, w,
	uint32_t, tm, uint32_t, cl);
XY_DEF(sic_str_t, on_empty_tile, view_tile_t, t, unsigned, side, sic_str_t, ss);

XY_DEF(int, on_new_player, unsigned, player_ref);
XY_DEF(int, on_auth, unsigned, player_ref);
XY_DEF(int, on_before_leave, unsigned, ent_ref);
XY_DEF(int, on_leave, unsigned, player_ref, unsigned, loc_ref);
XY_DEF(int, on_enter, unsigned, player_ref, unsigned, loc_ref);
XY_DEF(int, on_after_enter, unsigned, player_ref);
XY_DEF(int, on_spawn, unsigned, player_ref, unsigned, loc_ref, struct bio, bio,
	uint64_t, v);
XY_DEF(int, on_get, unsigned, player_ref, unsigned, ref);

/* fire the player-arrival events for a freshly connected player */
void
nd_event_announce(unsigned player_ref, unsigned loc_ref)
{
	on_new_player(player_ref);
	on_enter(player_ref, loc_ref);
}