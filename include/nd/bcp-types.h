#ifndef ND_BCP_TYPES_H
#define ND_BCP_TYPES_H

/*
 * bcp-types.h — Binary Client Protocol (BCP `#b`) types, opcodes, and data structures.
 * Two-header separation rule: contains ZERO XY_DECLs so implementers can include it.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic frame prefix */
#define ND_BCP_MAGIC_0 '#'
#define ND_BCP_MAGIC_1 'b'

/* Core BCP channel identifiers (iden) */
enum nd_bcp_opcode {
	ND_BCP_ITEM         = 0,
	ND_BCP_VIEW         = 1,
	ND_BCP_VIEW_BUFFER  = 2,
	ND_BCP_ROOM         = 3,
	ND_BCP_ENTITY       = 4,
	ND_BCP_AUTH_FAILURE = 5,
	ND_BCP_AUTH_SUCCESS = 6,
	ND_BCP_OUT          = 7,
	ND_BCP_TOD          = 8,
	ND_BCP_ACTION       = 9,
	ND_BCP_REGION       = 10,

	/* Dynamic channels on HD_BCP: numbers >= 14 are owned by the tenant
	 * module that registers them and decoded via on_bcp_channel. */
	ND_BCP_CHANNEL_DEF  = 14,
	ND_BCP_EXT_MSG      = 15,
};

#define ND_MAX_ACTIONS  32
#define ND_MAX_ENTITIES 64
#define ND_MAX_LINE     1024

/*
 * Generic per-module client-state extension slots.
 *
 * axil-nd knows nothing about what tenant modules store: each module claims
 * a named slot (keyed by its own module name) and interprets the bytes as its
 * own type, defined in that module's headers. Slots live inside
 * nd_client_state_t, so state stays per-session with no globals.
 */
#define ND_EXT_SLOTS     8
#define ND_EXT_KEY_MAX   16
#define ND_EXT_DATA_U64  64 /* 512 bytes, 8-byte aligned */

typedef struct {
	char     key[ND_EXT_KEY_MAX];
	uint64_t data[ND_EXT_DATA_U64];
} nd_ext_slot_t;

typedef struct {
	uint16_t val;
	uint16_t max;
} nd_bar_state_t;

/*
 * Generic named gauges (bars). The core client knows nothing about what a
 * gauge means: tenant modules name and publish them dynamically.
 */
#define ND_MAX_GAUGES      8
#define ND_GAUGE_NAME_MAX  8

typedef struct {
	char           name[ND_GAUGE_NAME_MAX];
	nd_bar_state_t bar;
} nd_gauge_t;

typedef struct {
	uint32_t id;
	char label[32];
	char icon[8];
} nd_action_desc_t;

typedef struct {
	uint32_t ref;
	uint32_t location;
	uint8_t  dynflags;
	uint32_t type;
	uint32_t actions;
	char     name[32];
	char     unparse[32];
	uint16_t fg;
	uint16_t flags;
	char     ch;
	char     art[64];
} nd_entity_desc_t;

typedef struct {
	uint32_t ref;
	char     name[64];
	char     desc[256];
	uint32_t exits;
} nd_room_desc_t;

typedef struct {
	uint32_t me;
	int auth_failed;
	int auth_reason;
	uint32_t current_room;
	uint32_t target_ref;        /* generic client-side current target */
	uint32_t selected_item_ref;
	uint32_t tod;
	nd_gauge_t gauges[ND_MAX_GAUGES];
	nd_ext_slot_t ext[ND_EXT_SLOTS];
	nd_action_desc_t actions[ND_MAX_ACTIONS];
	size_t action_count;
	char active_modal[32]; /* active modal identifier, module-defined, or "" */
	char user[64];
	char map_buf[1024];
	char preferred_driver[16];
	nd_room_desc_t room;
	nd_entity_desc_t entities[ND_MAX_ENTITIES];
	size_t entity_count;
} nd_client_state_t;

/* Read a gauge by name; NULL if no module has published it. */
/* Conventional gauge name for `target_ref`. */
#define ND_CLIENT_TARGET_GAUGE "target"

static inline const nd_bar_state_t *
nd_client_gauge_get(const nd_client_state_t *st, const char *name)
{
	if (!st || !name || !name[0])
		return NULL;
	for (size_t i = 0; i < ND_MAX_GAUGES; i++)
		if (st->gauges[i].name[0] && strncmp(st->gauges[i].name, name, ND_GAUGE_NAME_MAX) == 0)
			return &st->gauges[i].bar;
	return NULL;
}

/* Create-or-update a gauge; returns 0 on success, -1 if the table is full
 * or the name is empty/too long. */
static inline int
nd_client_gauge_set(nd_client_state_t *st, const char *name, uint16_t val, uint16_t max)
{
	if (!st || !name || !name[0] || strlen(name) >= ND_GAUGE_NAME_MAX)
		return -1;
	for (size_t i = 0; i < ND_MAX_GAUGES; i++) {
		if (st->gauges[i].name[0] && strncmp(st->gauges[i].name, name, ND_GAUGE_NAME_MAX) == 0) {
			st->gauges[i].bar.val = val;
			st->gauges[i].bar.max = max;
			return 0;
		}
	}
	for (size_t i = 0; i < ND_MAX_GAUGES; i++) {
		if (!st->gauges[i].name[0]) {
			memcpy(st->gauges[i].name, name, strlen(name) + 1);
			st->gauges[i].bar.val = val;
			st->gauges[i].bar.max = max;
			return 0;
		}
	}
	return -1;
}

/* Look up an extension slot without creating it; NULL if absent. */
static inline const void *
nd_client_ext_get(const nd_client_state_t *st, const char *key)
{
	if (!st || !key || !key[0])
		return NULL;
	for (size_t i = 0; i < ND_EXT_SLOTS; i++)
		if (st->ext[i].key[0] && strncmp(st->ext[i].key, key, ND_EXT_KEY_MAX) == 0)
			return st->ext[i].data;
	return NULL;
}

/* Claim (or find) a zero-initialised extension slot of at least `size` bytes;
 * NULL if the table is full or `size` exceeds the slot capacity. */
static inline void *
nd_client_ext(nd_client_state_t *st, const char *key, size_t size)
{
	if (!st || !key || !key[0] || size > sizeof(st->ext[0].data)
	    || strlen(key) >= ND_EXT_KEY_MAX)
		return NULL;
	for (size_t i = 0; i < ND_EXT_SLOTS; i++)
		if (st->ext[i].key[0] && strncmp(st->ext[i].key, key, ND_EXT_KEY_MAX) == 0)
			return st->ext[i].data;
	for (size_t i = 0; i < ND_EXT_SLOTS; i++) {
		if (!st->ext[i].key[0]) {
			memset(&st->ext[i], 0, sizeof(st->ext[i]));
			memcpy(st->ext[i].key, key, strlen(key) + 1);
			return st->ext[i].data;
		}
	}
	return NULL;
}

/* Helper to safely read little-endian uint16 */
static inline uint16_t nd_bcp_read_u16(const uint8_t *p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Helper to safely read little-endian uint32 */
static inline uint32_t nd_bcp_read_u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Helper to safely read null-terminated string with bounds check */
static inline int nd_bcp_read_str(const uint8_t **p, size_t *rem, char *dst, size_t dst_cap) {
	if (!p || !*p || !rem || !dst || dst_cap == 0) return 0;
	size_t len = 0;
	const uint8_t *cur = *p;
	while (len < *rem && cur[len] != '\0')
		len++;
	if (len >= *rem) return 0; /* Unterminated string */

	size_t copy_len = (len < dst_cap - 1) ? len : dst_cap - 1;
	memcpy(dst, cur, copy_len);
	dst[copy_len] = '\0';
	*p += (len + 1);
	*rem -= (len + 1);
	return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* ND_BCP_TYPES_H */
