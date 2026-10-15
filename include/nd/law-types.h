#ifndef ND_LAW_TYPES_H
#define ND_LAW_TYPES_H

/*
 * law-types.h — neutral value types for the axil-nd host / law layer.
 *
 * Genuinely neutral primitives: handle mechanism, storage callback types,
 * storage flags, command callback signature, and basic sizing. Zero game
 * vocabulary (no OBJ, ENT, SKEL, room, bio, or terrain).
 */

#include <stddef.h>
#include <stdint.h>

/* --- handle mechanism ---------------------------------------------------- */

#define ND_HD_MOD ((unsigned) 0x80000000)
#define ND_HD_TABLE_MAX 64
#define ND_HD_FD 0

extern unsigned nd_hds[];

static inline unsigned nd_hd_mod(unsigned idx) { return ND_HD_MOD | idx; }
static inline int nd_hd_is_mod(unsigned hd) { return (hd & ND_HD_MOD) != 0; }
static inline unsigned nd_hd_mod_idx(unsigned hd) { return hd & ~ND_HD_MOD; }

unsigned hd_resolve(unsigned hd);

/* --- secondary index callback -------------------------------------------- */

typedef int nd_assoc_cb_t(const void ** const skey,
		const void * const key,
		const void * const data);

typedef nd_assoc_cb_t *nd_assoc_cb_p;

/* --- nd_open() flags (mirrors corm CM_* values) -------------------------- */

#define ND_AINDEX 1
#define ND_PGET   4
#define ND_SEC    8

/* --- asset kinds for generic resolver ----------------------------------- */

enum nd_asset_kind {
	ND_ASSET_HTDOCS = 0,
	ND_ASSET_ART,
	ND_ASSET_MAN,
	ND_ASSET_MOTD
};

/* --- command callback signature ----------------------------------------- */

typedef void nd_cb_t(int fd, int argc, char *argv[]);

#endif /* ND_LAW_TYPES_H */
