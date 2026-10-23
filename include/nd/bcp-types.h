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

	/* Standard dynamic channel assignments on HD_BCP */
	ND_BCP_HP           = 10,
	ND_BCP_STATS        = 11,
	ND_BCP_MP           = 13,
	ND_BCP_CHANNEL_DEF  = 14,
	ND_BCP_EXT_MSG      = 15,
};

/* Attribute indices in nd_stats_state_t.attr */
enum nd_attr_idx {
	ND_ATTR_STR = 0,
	ND_ATTR_CON = 1,
	ND_ATTR_DEX = 2,
	ND_ATTR_INT = 3,
	ND_ATTR_WIZ = 4,
	ND_ATTR_CHA = 5,
	ND_ATTR_MAX = 6,
};

#define ND_MAX_ACTIONS  32
#define ND_MAX_ENTITIES 64
#define ND_MAX_LINE     1024

typedef struct {
	uint16_t val;
	uint16_t max;
} nd_bar_state_t;

typedef struct {
	uint32_t id;
	char label[32];
	char icon[8];
} nd_action_desc_t;

typedef struct {
	uint16_t attr[ND_ATTR_MAX];
	uint32_t dodge;
	uint32_t dmg;
	uint32_t mdmg;
	uint32_t def;
	uint32_t mdef;
} nd_stats_state_t;

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
	uint32_t target_ref;
	uint32_t tod;
	nd_bar_state_t hp;
	nd_bar_state_t mp;
	nd_stats_state_t stats;
	nd_action_desc_t actions[ND_MAX_ACTIONS];
	size_t action_count;
	char active_modal[32]; /* active modal identifier, e.g. "stats", "equip", or "" */
	nd_room_desc_t room;
	nd_entity_desc_t entities[ND_MAX_ENTITIES];
	size_t entity_count;
} nd_client_state_t;

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
