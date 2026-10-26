#ifndef ND_BCP_H
#define ND_BCP_H

/*
 * bcp.h — Caller-facing header for Binary Client Protocol decoding and hooks.
 * Implementers include <nd/bcp-types.h>, not this header.
 */

#include <ttypt/xy.h>
#include <stdio.h>
#include <string.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hook for extension BCP channels (tenant-module defined).
 * Returns 1 if handled, -1 if malformed, 0 to propagate.
 */
XY_DECL(int, on_bcp_channel, uint8_t, opcode, const uint8_t *, payload, size_t, len, void *, state);

/*
 * Decode a BCP frame into nd_client_state_t.
 * Returns:
 *   1 if a state-affecting packet was decoded,
 *   0 if not a BCP packet (e.g. raw ANSI text),
 *  -1 on malformed BCP frame.
 */
static inline int
nd_bcp_decode(const uint8_t *buf, size_t len, nd_client_state_t *st)
{
	if (!buf || len < 3 || !st)
		return 0;
	if (buf[0] != ND_BCP_MAGIC_0 || buf[1] != ND_BCP_MAGIC_1)
		return 0;

	uint8_t iden = buf[2];
	const uint8_t *p = buf + 3;
	size_t rem = len - 3;

	switch (iden) {
	case ND_BCP_ROOM:
		return nd_bcp_unpack_room_msg(buf, len, &st->room);

	case ND_BCP_ITEM: {
		nd_entity_desc_t item;
		memset(&item, 0, sizeof(item));
		int rc = nd_bcp_unpack_entity_msg(buf, len, &item);
		if (rc != 1)
			return rc;

		if (item.dynflags == 1) {
			if (item.type == 0) { /* TYPE_ROOM */
				if (st->room.ref != item.ref) {
					/* Entering a new room: clear entities from old rooms (keep player & inventory) */
					size_t keep = 0;
					for (size_t i = 0; i < st->entity_count; i++) {
						if (st->entities[i].ref == st->me ||
						    (st->me && st->entities[i].location == st->me) ||
						    st->entities[i].location == item.ref) {
							st->entities[keep++] = st->entities[i];
						}
					}
					st->entity_count = keep;
				}
				st->room.ref = item.ref;
				snprintf(st->room.name, sizeof(st->room.name), "%s", item.name);
				st->current_room = item.ref;
				st->target_ref = 0;
				return 1;
			} else {
				st->target_ref = item.ref;
			}
		}

		size_t slot = st->entity_count;
		for (size_t i = 0; i < st->entity_count; i++) {
			if (st->entities[i].ref == item.ref) {
				slot = i;
				break;
			}
		}
		if (slot >= ND_MAX_ENTITIES) {
			/* Eviction: Evict first entity that is NOT the player and NOT in current room */
			size_t evict = 0;
			int found = 0;
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref != st->me && st->room.ref && st->entities[i].location != st->room.ref) {
					evict = i;
					found = 1;
					break;
				}
			}
			if (!found) {
				for (size_t i = 0; i < st->entity_count; i++) {
					if (st->entities[i].ref != st->me) {
						evict = i;
						break;
					}
				}
			}
			for (size_t i = evict; i + 1 < st->entity_count; i++)
				st->entities[i] = st->entities[i + 1];
			st->entity_count--;
			slot = st->entity_count;
		}

		st->entities[slot] = item;
		if (slot == st->entity_count)
			st->entity_count++;
		return 1;
	}

	case ND_BCP_OUT: {
		uint32_t out_ref = 0;
		if (nd_bcp_unpack_u32_frame(buf, len, &out_ref) == 1) {
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref == out_ref) {
					for (size_t j = i; j + 1 < st->entity_count; j++)
						st->entities[j] = st->entities[j + 1];
					st->entity_count--;
					break;
				}
			}
			return 1;
		}
		break;
	}

	case ND_BCP_AUTH_SUCCESS:
		if (nd_bcp_unpack_u32_frame(buf, len, &st->me) == 1) {
			st->auth_failed = 0;
			return 1;
		}
		break;

	case ND_BCP_AUTH_FAILURE: {
		uint32_t reason = 0;
		if (nd_bcp_unpack_u32_frame(buf, len, &reason) == 1) {
			st->auth_reason = (int)reason;
			st->auth_failed = 1;
			return 1;
		}
		break;
	}

	case ND_BCP_VIEW: {
		if (rem >= sizeof(nd_map_data_t)) {
			memcpy(&st->map_data, p, sizeof(nd_map_data_t));
			return 1;
		}
		break;
	}

	case ND_BCP_VIEW_BUFFER: {
		size_t copylen = (rem < sizeof(st->map_buf) - 1) ? rem : (sizeof(st->map_buf) - 1);
		memcpy(st->map_buf, p, copylen);
		st->map_buf[copylen] = '\0';
		return 1;
	}

	case ND_BCP_TOD:
		return nd_bcp_unpack_u32_frame(buf, len, &st->tod);

	case ND_BCP_REGION: {
		nd_bcp_reader_t r;
		nd_bcp_reader_init(&r, p, rem);
		char stage_drv[16] = {0};
		char map_drv[16] = "ascii-bud";
		uint8_t layout = ND_LAYOUT_STANDARD_HYBRID;
		uint8_t flags = ND_REGION_ALLOW_CHOICE;

		nd_bcp_read_str_c(&r, stage_drv, sizeof(stage_drv));
		if (nd_bcp_reader_ok(&r)) {
			strncpy(st->preferred_driver, stage_drv, sizeof(st->preferred_driver) - 1);
			st->preferred_driver[sizeof(st->preferred_driver) - 1] = '\0';

			if (nd_bcp_reader_rem(&r) > 0) {
				char m[16] = {0};
				nd_bcp_read_str_c(&r, m, sizeof(m));
				if (m[0])
					strncpy(map_drv, m, sizeof(map_drv) - 1);
			}
			if (nd_bcp_reader_rem(&r) > 0)
				layout = nd_bcp_read_u8(&r);
			if (nd_bcp_reader_rem(&r) > 0)
				flags = nd_bcp_read_u8(&r);

			strncpy(st->preferred_map_driver, map_drv, sizeof(st->preferred_map_driver) - 1);
			st->preferred_map_driver[sizeof(st->preferred_map_driver) - 1] = '\0';
			st->layout_mode = layout;
			st->pres_flags = flags;
			return 1;
		}
		break;
	}

	case ND_BCP_ACTION: {
		nd_bcp_reader_t r;
		nd_bcp_reader_init(&r, p, rem);
		uint32_t id = nd_bcp_read_u32_c(&r);
		char label[32] = {0};
		char icon[16] = {0};
		nd_bcp_read_str_c(&r, label, sizeof(label));
		nd_bcp_read_str_c(&r, icon, sizeof(icon));
		if (!nd_bcp_reader_ok(&r))
			break;

		size_t slot = st->action_count;
		for (size_t i = 0; i < st->action_count; i++) {
			if (st->actions[i].id == id) {
				slot = i;
				break;
			}
		}
		if (slot >= ND_MAX_ACTIONS)
			return 0;

		st->actions[slot].id = id;
		memcpy(st->actions[slot].label, label, sizeof(st->actions[slot].label));
		memcpy(st->actions[slot].icon, icon, sizeof(st->actions[slot].icon));

		if (slot == st->action_count)
			st->action_count++;
		return 1;
	}

	default: {
		int rc = on_bcp_channel(iden, p, rem, st);
		if (rc != 0)
			return rc;
		break;
	}
	}

	return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* ND_BCP_H */
