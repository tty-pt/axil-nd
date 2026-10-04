#ifndef UAPI_OBJECT_H
#define UAPI_OBJECT_H

#include "./azoth.h"
#include <stdint.h>

#define ROOT ((unsigned) 1)
#define NOTHING ((unsigned) -1)

enum object_flags {
	OF_PLAYER = 1,
	OF_INF = 1,
};

enum room_flags {
	RF_TEMP = 1,
	RF_HAVEN = 2,
};

enum exit {
	E_NULL = 0,
	E_WEST = 1,
	E_NORTH = 2,
	E_UP = 4,
	E_EAST = 8,
	E_SOUTH = 16,
	E_DOWN = 32,
	E_ALL = 63,
};

typedef struct {
	unsigned flags;
	unsigned char exits;
	unsigned char doors;
	unsigned char floor;
} ROO;

/* EF_WIZARD (8) was deleted: it was never set by any code path, so every
 * st_is_wiz() gate on it was dead code. Authority is now scoped to a region
 * (see ST.md §27.6(1) and NO_WIZ.md). Bit 8 is deliberately left unallocated
 * rather than reserved -- no persisted flag outgrew it, and a free bit that
 * nothing sets is indistinguishable from a reserved one until someone reads
 * the gap as an invitation. */
enum entity_flags {
	EF_SHOP = 4,
};

/* EF_BAN (16) was deleted next: the ban table supersedes the bit (ST.md
 * §27.6(1) §7, NO_WIZ.md). Bit 16 is left unallocated for the same reason as
 * bit 8 above. The boot migration (st_ban_migrate) still READS legacy rows
 * carrying 16 -- it uses the EF_BAN_LEGACY literal in spacetime.c, not this
 * enum -- so an upgrade converts old bans instead of dropping them. */

enum base_actions {
	ACT_LOOK = 1,
	ACT_OPEN = 2,
	ACT_GET = 4,
	/* ACT_DROP was lost when this engine was split out of the old nd and
	 * has never come back, so nd-core's on_icon -- the module that builds
	 * every icon -- could not be ported at all. The value is not a guess:
	 * it is the old engine's own ACT_DROP, and `ico.actions` is memcpy'd
	 * into the BCP frame (src/mcp.c:116) as a raw int, so 8 is a WIRE value
	 * the NeverDark client already decodes. Verified by diffing this enum
	 * against /home/quirinpa/nd/include/uapi/object.h: ACT_DROP = 8 was the
	 * only line missing. Additive, so no existing value moves. Keep
	 * nd/xy-types.h's copy in step -- modules compile against that one,
	 * and the two were verified byte-identical. */
	ACT_DROP = 8,
};

typedef struct entity {
	unsigned home;
	unsigned flags;

	/* tmp data? */
	unsigned last_observed;
	unsigned char select;
	unsigned char aux;
} ENT;

typedef struct object {
	unsigned location, owner;

	unsigned skid;
	unsigned art_id;
	unsigned char type;
	unsigned value;
	unsigned flags;
	char name[32];
	unsigned data[8];
} OBJ;

struct icon {
	int actions;
	struct print_info pi;
	char ch;
};

/* FIXME: not for plugins */
extern unsigned obj_hd, contents_hd, obs_hd;

typedef int obj_exists_t(unsigned ref);
obj_exists_t eng_obj_exists;

typedef unsigned object_new_t(OBJ *obj);
object_new_t eng_object_new;

typedef unsigned object_copy_t(OBJ *nu, unsigned old_ref);
object_copy_t eng_object_copy;

typedef void object_move_t(unsigned what_ref, unsigned where_ref);
object_move_t eng_object_move;

typedef unsigned object_add_t(OBJ *nu, unsigned skel_id, unsigned where, uint64_t v, unsigned flags);
object_add_t eng_object_add;

typedef void object_drop_t(unsigned where_ref, unsigned skel_id);
object_drop_t eng_object_drop;

typedef struct icon object_icon_t(unsigned player_ref, unsigned thing_ref);
object_icon_t eng_object_icon;

typedef char *object_art_t(unsigned ref);
object_art_t eng_object_art;

typedef const char *unparse_t(unsigned loc_ref);
unparse_t eng_unparse;

#endif
