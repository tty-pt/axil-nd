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

	/* MODS.md §0.2: the two handle namespaces. */

	/* nd_open returns a TAGGED handle, and the same handle reads back
	 * through nd_put/nd_get -- so a module's own table is usable and
	 * cannot collide with an enum hd. "u"/"u" is a CM_U32 key/value table;
	 * nd-class and friends open "p"/"p" because they store pointers, which
	 * is not what this probe round-trips. */
	{
		unsigned h = nd_open("demo_probe", "u", "u", 0);
		unsigned key = 42, val = 99, back = 0;
		int ok = 0;

		WARN("demo nd_open -> 0x%x%s\n", h,
			nd_hd_is_mod(h) ? " (tagged)" : " (UNTAGGED -- bug)");

		if (nd_hd_is_mod(h)) {
			nd_put(h, &key, &val);
			ok = nd_get(h, &back, &key) == 0 && back == 99;
		}
		WARN("demo nd_open/nd_put/nd_get round trip: %s\n",
			ok ? "ok" : "FAILED");
	}

	/* A handle in neither namespace must miss, not scribble. HD_MAX is
	 * just past the enum; an unopened module tag is just past the
	 * registry. */
	{
		unsigned key = 42, back = 0;
		int en = nd_get(HD_MAX, &back, &key);
		int mod = nd_get(nd_hd_mod(9999), &back, &key);
		WARN("demo out-of-range handles: enum=%s mod=%s\n",
			en ? "missed (ok)" : "HIT (bug)",
			mod ? "missed (ok)" : "HIT (bug)");
	}

	/* nd_printf (MODS.md §0.3): format and write in one call. */
	{
		/* player_ref 0 is a placeholder at install time; the real
		 * check is the visible frame from on_enter below. */
		nd_printf(0, "demo nd_printf built %d + %d = %d\n", 2, 2, 4);
		WARN("demo nd_printf compiled and dispatched\n");
	}

	/* nd_last (MODS.md §0.3): reads the injected xy context, so if
	 * <ttypt/xy-mod.h> were not included first this would not compile.
	 * A non-hook context has no dispatch in flight, so 0 (nothing ran) is
	 * the correct answer here -- the point is that the symbol resolves. */
	{
		int dummy = 0;
		int ran = nd_last(&dummy);
		WARN("demo nd_last resolved (ran=%d)\n", ran);
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

	/* MODS.md §0.2, end to end: HD_OBJ must resolve to the engine's
	 * object table. player_ref is a live object ref here, so a real name
	 * comes back. Pre-0.2 this passed HD_OBJ (== 7) straight to corm_get
	 * as a table number, which read whatever corm table 7 happened to be
	 * -- an empty name, or a match by luck. */
	{
		OBJ obj;
		int rc;
		memset(&obj, 0, sizeof(obj));
		rc = nd_get(HD_OBJ, &obj, &player_ref);
		WARN("demo nd_get(HD_OBJ, ref=%u) rc=%u name='%s' %s\n",
			player_ref, rc, obj.name,
			(rc == 0 && obj.name[0]) ? "resolved (ok)"
			                         : "NOT RESOLVED (bug)");

		/* nd_printf to a live player, so the formatted text lands on the
		 * wire and not just in a WARN. Returns the byte count. */
		nd_printf(player_ref, "[demo] nd_printf %s is in %s\n",
		          obj.name[0] ? obj.name : "(anon)", "the room");
	}
	return 0;
}