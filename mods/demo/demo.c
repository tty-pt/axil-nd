/* demo.c — sample ND game module on libxylem (miniature main.so replacement).
 *
 * Validates the J4.4/J4.8-step-2 contract:
 *   - includes the shared game API header freely (papi/nd-xy.h, XY_DECL),
 *   - calls the engine-provided io hooks as plain C (dispatches via XY_CALL
 *     to libaxil-nd.so's XY_IMPL providers),
 *   - implements game event hooks (on_demo, on_enter) that the engine fires.
 *     (A TU that XY_IMPLs an event must NOT include papi/nd-hooks.h for it —
 *     same XY_DECL/XY_IMPL rule as nd-xy.h.)
 *
 * Build (mirrors module.mk lineage; ndcc injects papi/nd-xy.h + module.ld):
 *   cc -shared -fPIC -I../include -I/usr/include -o demo.so demo.c \
 *        -Wl,--no-execute-only
 *
 * The engine xy_load()s this .so from mods/ via the mods.load list file in
 * its xy_install().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/xy-mod.h>

#include "papi/nd-xy.h"

/* A game event the engine fires. Modules XY_IMPL it; the engine XY_DEFs it in
 * src/nd_xy.c (a module must NOT XY_DECL a hook it XY_IMPLs in the same TU). */

XY_MODULE_API void
xy_install(void)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "demo module loaded (module_path '%s')\n",
	         xy.module_path);

	/* call into the engine io provider exactly like game code would */
	nd_write(42, buf, strlen(buf));
	WARN("demo xy_install ran, nd_write dispatched\n");

	/* exercise a sampling of the expanded service surface (stubs until
	 * the engine TUs link in): results unused, dispatch is the proof */
	{
		ENT e = ent_get(1);
		unsigned me = me_get(0);
		int has = map_has(1);
		unsigned m = ematch_player("demo");
		unsigned a = action_register("demo-look", "o");
		(void)e; (void)me; (void)has; (void)m; (void)a;
		WARN("demo xy_install exercised ent_get/me_get/map_has/ematch/action_register\n");
	}
}

/* engine → module hook: dispatched to every demo module in the region */
XY_IMPL(int, on_demo, unsigned, player_ref, char *, message)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "[demo] player %u says: %s\n",
	         player_ref, message);
	nd_write(player_ref, buf, strlen(buf));
	WARN("demo on_demo dispatched for player %u\n", player_ref);
	return 0;
}

/* SIC lifecycle event from papi/nd-hooks.h (engine XY_DEFs it in
 * src/nd_events.c, fires on connect via nd_event_announce) */
XY_IMPL(int, on_enter, unsigned, player_ref, unsigned, loc_ref)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "[demo] on_enter player %u at %u\n",
	         player_ref, loc_ref);
	nd_write(player_ref, buf, strlen(buf));
	WARN("demo on_enter dispatched for player %u\n", player_ref);
	return 0;
}