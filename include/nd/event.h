#ifndef ND_EVENT_H
#define ND_EVENT_H

/*
 * event.h — Canonical typed XY hooks for heterogeneous game events.
 *
 * Provides bus-level XY declarations for game events with non-integer return
 * types or heterogeneous argument structures (noise bio calculations, empty tile
 * string generation, icon resolution, view flags, spawn, and room transitions).
 */

#include <stdint.h>
#include <ttypt/xy.h>
#include <nd/world-types.h>
#include <nd/view-types.h>

/* Heterogeneous Game Event Hooks */
XY_DECL(struct bio, nd_event_noise, struct bio, bio, uint32_t, he, uint32_t, w,
	uint32_t, tm, uint32_t, cl);
XY_DECL(sic_str_t, nd_event_empty_tile, view_tile_t, t, unsigned, side,
	sic_str_t, ss);
XY_DECL(struct icon, nd_event_icon, unsigned, ref, unsigned, type, unsigned, player_ref);
XY_DECL(unsigned short, nd_event_view_flags, unsigned short, flags, unsigned, ref);
XY_DECL(int, nd_event_spawn, unsigned, player_ref, unsigned, loc_ref, struct bio, bio,
	uint64_t, v);
XY_DECL(int, nd_event_update, unsigned, ref, unsigned, type, double, dt);
XY_DECL(int, nd_event_vim, unsigned, ref, sic_str_t, ss);
XY_DECL(int, nd_event_room, unsigned, player_ref, unsigned, loc_ref, int, is_enter);
XY_DECL(int, nd_event_entity, unsigned, ref, int, evt_type);

#endif /* ND_EVENT_H */
