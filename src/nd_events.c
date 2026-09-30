/* nd_events.c — engine XY_DEF side for the papi/nd-hooks.h game events.
 *
 * Each XY_DEF emits the canonical adapter + a static inline dispatcher that
 * runs every listener in the current region. Game modules XY_IMPL the
 * listeners they want.
 * Folded into src/libaxil-nd.c (single-TU module): do NOT compile standalone
 * and do NOT include papi/nd-hooks.h here (XY_DEF + XY_DECL clash).
 *
 * WHY THE nd_evt_* WRAPPERS
 * -------------------------
 * The engine's event firing sites live in ordinary engine TUs (object.c,
 * entity.c, spacetime.c, world.c, ...) which CANNOT include papi/nd-hooks.h:
 * papi/nd-xy-types.h redefines struct icon / enum color / sic_str_t that the
 * uapi headers already define, and the same symbols then appear twice. So the
 * engine calls exported wrappers instead, declared in uapi/type.h.
 *
 * Two reasons not to use XY_DECL at the call sites:
 *
 *  1. Size. XY_DECL emits a static xy_adapter_t per hook per TU. xy_adapter_t
 *     is 4200 bytes (4096 of it the `ret` scratch buffer), so the 20 events
 *     would add ~84 KB of .data to every engine TU that includes
 *     uapi/type.h — 1.2 MB of pure padding in the .so once eight TUs pull it
 *     in, relocated on every dlopen.
 *
 *  2. Region. xy_call() dispatches from the *thread-local current region*, not
 *     from the caller's identity. Every wrapper below lives in the one TU that
 *     holds libaxil-nd's injected module context (xy, from xy-mod.h), so
 *     events always fire from the engine's own region subtree. Game modules
 *     loaded by nd_mods_load() are children of exactly that region, which is
 *     what makes them see the events at all.
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

/* --- engine-facing fire functions (uapi/type.h) -------------------------- */
/* One per event above. Signatures must stay ABI-identical to the XY_DEFs and
 * to the uapi/type.h prototypes; the body is just the generated inline, which
 * routes through xy_call and returns the last listener's value (0 when no
 * module implements the event). */

int nd_evt_status(unsigned player_ref) { return on_status(player_ref); }
int nd_evt_examine(unsigned player_ref, unsigned ref, unsigned type)
{ return on_examine(player_ref, ref, type); }
int nd_evt_add(unsigned ref, unsigned type, uint64_t v)
{ return on_add(ref, type, v); }
unsigned short nd_evt_view_flags(unsigned short flags, unsigned ref)
{ return on_view_flags(flags, ref); }
struct icon nd_evt_icon(unsigned ref, unsigned type, unsigned player_ref)
{ return on_icon(ref, type, player_ref); }
int nd_evt_del(unsigned ref, unsigned type) { return on_del(ref, type); }
int nd_evt_clone(unsigned orig_ref, unsigned nu_ref)
{ return on_clone(orig_ref, nu_ref); }
int nd_evt_update(unsigned ref, unsigned type, double dt)
{ return on_update(ref, type, dt); }
int nd_evt_move(unsigned ref) { return on_move(ref); }
int nd_evt_vim(unsigned ref, sic_str_t ss) { return on_vim(ref, ss); }
struct bio nd_evt_noise(struct bio bio, uint32_t he, uint32_t w, uint32_t tm,
	uint32_t cl)
{ return on_noise(bio, he, w, tm, cl); }
sic_str_t nd_evt_empty_tile(view_tile_t t, unsigned side, sic_str_t ss)
{ return on_empty_tile(t, side, ss); }
int nd_evt_new_player(unsigned player_ref)
{ return on_new_player(player_ref); }
int nd_evt_auth(unsigned player_ref) { return on_auth(player_ref); }
int nd_evt_before_leave(unsigned ent_ref)
{ return on_before_leave(ent_ref); }
int nd_evt_leave(unsigned player_ref, unsigned loc_ref)
{ return on_leave(player_ref, loc_ref); }
int nd_evt_enter(unsigned player_ref, unsigned loc_ref)
{ return on_enter(player_ref, loc_ref); }
int nd_evt_after_enter(unsigned player_ref)
{ return on_after_enter(player_ref); }
int nd_evt_spawn(unsigned player_ref, unsigned loc_ref, struct bio bio,
	uint64_t v)
{ return on_spawn(player_ref, loc_ref, bio, v); }
int nd_evt_get(unsigned player_ref, unsigned ref)
{ return on_get(player_ref, ref); }

/* Fire the arrival event for a player who has just been placed in a room.
 *
 * Only on_enter. on_new_player is NOT fired here: it belongs to first
 * creation and fires exactly once, from nd_player_login() in world.c, for
 * every code path (WS and raw) alike. This used to fire both, which is why
 * the engine kept a call_on_new_player() of its own in world.c and why demo
 * saw a second on_enter — the pair has been split up now that the engine's own
 * sites reach XY.
 *
 * A brand-new player is created with where=NOTHING (world.c:776), so
 * object_add()'s own on_enter never runs for them: landing a player is not the
 * same event as moving an object into a room, and this is the site that says
 * so. */
void
nd_event_announce(unsigned player_ref, unsigned loc_ref)
{
	on_enter(player_ref, loc_ref);
}
