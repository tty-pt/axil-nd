#ifndef PAPI_ND_XY_TYPES_H
#define PAPI_ND_XY_TYPES_H

/*
 * nd-xy-types.h — shared value types for the NeverDark game API (XY).
 *
 * Defines the GAME data types that papi/nd-xy.h (service hooks) and
 * papi/nd-hooks.h (events) pass by value/pointer. It is a PASTE of the
 * struct/enum definitions from the engine's uapi headers WITHOUT any
 * fn-pointer globals (`ent_get_t ent_get;` etc.) or `extern unsigned *_hd;`
 * so game modules and the engine provider TU can include it freely alongside
 * ttypt/xy.h.
 *
 * Layout must stay in sync with uapi headers (same field order/types). If the
 * engine schema drifts, update BOTH here and there.
 */

#include <stdint.h>
#include <stddef.h>

#include <ttypt/xy.h>

/* --- azoth.h ------------------------------------------------------------- */

enum color {
	BLACK,
	RED,
	YELLOW,
	GREEN,
	CYAN,
	BLUE,
	MAGENTA,
	WHITE
};

enum pi_flags {
	BOLD = 1,
};

struct print_info {
	enum color fg;
	unsigned flags;
};

/* --- object.h ------------------------------------------------------------ */

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

enum entity_flags {
	EF_SHOP = 4,
	EF_WIZARD = 8,
	EF_BAN = 16,
};

enum base_actions {
	ACT_LOOK = 1,
	ACT_OPEN = 2,
	ACT_GET = 4,
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

/* --- st.h ---------------------------------------------------------------- */

#define DIM 2
#define NOISE_MAX ((uint32_t) -1)
#define DAYTICK_Y	10

typedef int16_t coord_t;
typedef uint16_t ucoord_t;

typedef coord_t point_t[DIM];

typedef coord_t point4D_t[4];

typedef point4D_t pos_t;

typedef uint64_t morton_t;

struct cmd_dir {
	char dir;
	enum exit e;
	morton_t rep;
};

struct bio {
	coord_t tmp;
	ucoord_t rn;
	uint32_t ty;
	unsigned bio_idx;
	unsigned raw[16];
};

typedef struct view_tile {
	unsigned bio_idx;
	unsigned room;
	ucoord_t flags, exits, doors;
	unsigned raw[16];
} view_tile_t;

struct spawn_arg {
	unsigned where_ref;
	void *arg;
	coord_t *pos;
};

/* --- skel.h -------------------------------------------------------------- */

enum base_element {
	ELM_SPIRIT = 1,
	ELM_FIRE = 2,
	ELM_WATER = 4,
	ELM_AIR = 8,
	ELM_EARTH = 16,
	ELM_PHYSICAL = 32,
};

enum biome {
	BIOME_WATER = 0,
	BIOME_PERMANENT_ICE = 1,

	BIOME_TUNDRA = 2,
	BIOME_TUNDRA2,
	BIOME_TUNDRA3,
	BIOME_TUNDRA4,

	BIOME_COLD_DESERT,
	BIOME_SHRUBLAND,
	BIOME_CONIFEROUS_FOREST,
	BIOME_BOREAL_FOREST,

	BIOME_TEMPERATE_GRASSLAND,
	BIOME_WOODLAND,
	BIOME_TEMPERATE_SEASONAL_FOREST,
	BIOME_TEMPERATE_RAINFOREST,

	BIOME_DESERT,
	BIOME_SAVANNAH,
	BIOME_TROPICAL_SEASONAL_FOREST,
	BIOME_TROPICAL_RAINFOREST,
	BIOME_VOLCANIC = 18,
	BIOME_MAX = 19,
};

typedef struct drop {
	unsigned skel;
	unsigned char y, yield, yield_v;
} DROP;

typedef struct entity_skel {
	unsigned char y, flags;
	unsigned element;
	unsigned biomes;
} SENT;

enum type {
	TYPE_ROOM,
	TYPE_ENTITY,
};

typedef struct {
	const enum color bg;
} biome_skel_t;

typedef struct object_skel {
	char const name[32];
        enum type type;
	unsigned max_art;
	unsigned data[8];
} SKEL;

typedef struct {
	enum color color;
	unsigned weakness;
} element_t;

/* --- type.h -------------------------------------------------------------- */

#define ND_AINDEX 32

typedef struct {
	char str[256];
	int pos;
} sic_str_t;

typedef char small_buf_t[64];

typedef struct {
	struct print_info pi;
	char emp;
} vtf_t;

#endif /* PAPI_ND_XY_TYPES_H */