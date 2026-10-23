#ifndef ND_BCP_H
#define ND_BCP_H

/*
 * bcp.h — Caller-facing header for Binary Client Protocol decoding and hooks.
 * Implementers include <nd/bcp-types.h>, not this header.
 */

#include <ttypt/xy.h>
#include <nd/bcp-types.h>
#include <nd/bcp-meta.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hook for extension BCP channels (e.g. equipment, spells).
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

		size_t slot = st->entity_count;
		for (size_t i = 0; i < st->entity_count; i++) {
			if (st->entities[i].ref == item.ref) {
				slot = i;
				break;
			}
		}
		if (slot >= ND_MAX_ENTITIES)
			return 0;

		st->entities[slot] = item;
		if (slot == st->entity_count)
			st->entity_count++;
		return 1;
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

	case ND_BCP_HP:
		return nd_bcp_unpack_gauge(buf, len, &st->hp.val, &st->hp.max);

	case ND_BCP_MP:
		return nd_bcp_unpack_gauge(buf, len, &st->mp.val, &st->mp.max);

	case ND_BCP_TOD:
		return nd_bcp_unpack_u32_frame(buf, len, &st->tod);

	case ND_BCP_STATS: {
		nd_bcp_reader_t r;
		nd_bcp_reader_init(&r, p, rem);
		for (int i = 0; i < ND_ATTR_MAX; i++) {
			st->stats.attr[i] = nd_bcp_read_u16_c(&r);
		}
		st->stats.dodge = nd_bcp_read_u32_c(&r);
		st->stats.dmg   = nd_bcp_read_u32_c(&r);
		st->stats.mdmg  = nd_bcp_read_u32_c(&r);
		st->stats.def   = nd_bcp_read_u32_c(&r);
		st->stats.mdef  = nd_bcp_read_u32_c(&r);
		if (nd_bcp_reader_ok(&r))
			return 1;
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
