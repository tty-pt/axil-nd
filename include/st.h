#ifndef SPACETIME_H
#define SPACETIME_H

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ttypt/corm.h>
#include "uapi/object.h"
#include "uapi/st.h"

// adds 2^DAYTICK_Y to day tick until it reaches DAYSIZE
#define DAYTICK_Y	10
#define NIGHT_IS	(day_n == 1)

#define Y_COORD 0
#define X_COORD 1

#define COORD_MIN SHRT_MIN
#define COORD_MAX SHRT_MAX
#define UCOORD_MAX USHRT_MAX

#define DIM 2
#define WDIM X_COORD

#define OBITS(code) (code >> 48)

#define VIEW_AROUND 3
#define VIEW_SIZE ((VIEW_AROUND<<1) + 1)
#define VIEW_M VIEW_SIZE * VIEW_SIZE
#define VIEW_BDI (VIEW_SIZE * (VIEW_SIZE - 1))
#define MORTON_READ(pos) (* (morton_t *) pos)

typedef ucoord_t upoint_t[DIM];

typedef coord_t point3D_t[3];
typedef ucoord_t upoint3D_t[3];
typedef ucoord_t upoint4D_t[4];

typedef int64_t smorton_t;

struct rect {
	point_t s;
	upoint_t l;
};

struct rect4D {
	point4D_t s;
	upoint4D_t l;
};

typedef struct {
	const char name[16];
	const char other[16];
	enum exit simm;
	coord_t dim, dis;
} exit_t;

struct st_key {
	uint64_t key;
	unsigned shift;
} __attribute__((packed));

extern unsigned long long day_tick;
extern unsigned short day_n;
extern double tick;
extern enum exit e_map[];
extern exit_t exit_map[];

time_t get_tick(void);

morton_t pos_morton(pos_t);
void morton_pos(pos_t p, morton_t code);

int e_exit_can(OBJ *player, enum exit e);
int e_ground(unsigned room, enum exit e);

void pos_move(pos_t d, pos_t o, enum exit e);
enum exit dir_e(const char dir);
char e_dir(enum exit e);
enum exit e_simm(enum exit e);
char *e_name(enum exit e);
char *e_other(enum exit e);
morton_t point_rel_idx(point_t p, point_t s, smorton_t w);

void st_update(double dt);
int st_v(unsigned player_ref, const char *dir);

/* ST.md §22.1: a region is (id, plen), never id alone -- four regions share
 * id 0 (the root and its leftmost descendants at every width). plen is the
 * prefix width in the HIGH bits of id, so `shift` in the old st_key was
 * 64 - plen throughout; the shift-based spelling is kept only where the
 * legacy stchown/streload commands still speak it (§22.8). */
#define ST_SHIFT(plen) (64 - (unsigned)(plen))
#define ST_PLEN_ROOT  0    /* the cosmos: the whole keyspace */
#define ST_PLEN_WORLD 16   /* one world -- a planet */
#define ST_PLEN_CELL  64   /* one cell */

#define ND_ST_MAX_MODS 32
#define ND_ST_MOD_NAME 24

/* One corm row per region: owner, width, and the moderator's module set.
 *
 * owner/plen/nmods/flags are uint32_t rather than the uint8_t §7.2 sketched
 * because a CM_U32 corm record field memcpy's four bytes (libcorm.c:1517), so
 * a narrower field would be read past its own storage. mods is the flat
 * ND_ST_MAX_MODS x ND_ST_MOD_NAME block, addressed by nmods -- there is no
 * cross-element NUL guarantee, so nmods is the only safe bound (§22.2). */
struct st_rec {
	uint32_t owner;   /* OBJ ref of the ruler; NOTHING = unclaimed */
	uint32_t plen;    /* cross-check against the key's plen suffix (§22.2) */
	uint32_t nmods;   /* authoritative for iteration */
	uint32_t flags;   /* reserved */
	char     mods[ND_ST_MAX_MODS][ND_ST_MOD_NAME];
};

/* corm key for a region row: "<16 hex id><2 hex plen>", fixed width 18.
 *
 * Fixed width is what makes it parseable without sscanf's greedy-field
 * ambiguity, and parseable is required: a row must re-derive its own (id,
 * plen) to hand to xy_claim_at()/xy_with_region(), and a CM_U32 field is four
 * bytes so a uint64 id does not fit the field API at all. */
#define ST_ROW_KEY_LEN 18

static inline void
st_row_key(char *buf, size_t len, uint64_t id, uint8_t plen)
{
	snprintf(buf, len, "%016llx%02x", (unsigned long long)id, (unsigned)plen);
}

/* Hand-rolled rather than sscanf("%16llx%2x"): the width caps make that work
 * only if both conversions fill their field, and a short key would then be
 * silently accepted with a stale high word. */
static inline int
st_row_key_parse(const char *key, uint64_t *id, uint8_t *plen)
{
	unsigned long long v = 0;
	unsigned p = 0;

	if (!key || strlen(key) != ST_ROW_KEY_LEN)
		return -1;

	for (int i = 0; i < 16; i++) {
		char c = key[i];
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		v = (v << 4) | (unsigned long long)d;
	}
	for (int i = 16; i < ST_ROW_KEY_LEN; i++) {
		char c = key[i];
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		p = (p << 4) | (unsigned)d;
	}
	if (p > 64)
		return -1;

	*id = v;
	*plen = (uint8_t)p;
	return 0;
}

/* A planet IS a world: the complete 16-bit slice of the 4th coordinate is the
 * top 16 bits of pos_morton, so world N is the prefix region (N<<48, 16)
 * (§1.10, §2.3). */
static inline uint64_t st_planet_id(unsigned world)
{
	return (uint64_t)world << 48;
}

static inline unsigned st_world_of(uint64_t id)
{
	return (unsigned)(id >> 48);
}

void st_init(void);
void st_dlclose(void);
void eng_st_run(unsigned player_ref, char *symbol);
void do_stchown(int fd, int argc, char *argv[]);
void do_streload(int fd, int argc, char *argv[]);
void do_planet(int fd, int argc, char *argv[]);
void do_planets(int fd, int argc, char *argv[]);
void do_here(int fd, int argc, char *argv[]);
void do_loadmod(int fd, int argc, char *argv[]);
void do_unloadmod(int fd, int argc, char *argv[]);
void do_modlist(int fd, int argc, char *argv[]);
void do_release(int fd, int argc, char *argv[]);

/* Phase 2 region API (ST.md §22). Everything here speaks (id, plen). */

/* Register the st_rec record layout. Call once from nd_world_init BEFORE the
 * `st` table opens; the returned id feeds CM_RECORD(id) and
 * corm_record_type_id(). Idempotent. */
uint32_t st_rec_register(void);

/* One-field read of the owner -- st_high_shift does up to 65 of these per call
 * and must not copy a whole 784-byte row each time (§22.1). */
unsigned st_owner(uint64_t id, uint8_t plen);
int st_can(unsigned ref, uint64_t id, uint8_t plen);
int st_row_get(uint64_t id, uint8_t plen, struct st_rec *out);
void st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec);
void st_row_del(uint64_t id, uint8_t plen);

/* Create-or-reuse the region, then load/unload a module inside it. Both return
 * an XY_OK / XY_ERR_* code. A failed load never touches the row (§7.6). */
int st_region_load(uint64_t id, uint8_t plen, char *name);
int st_region_unload(uint64_t id, uint8_t plen, char *name);
int st_mod_loaded(uint64_t id, uint8_t plen, const char *name);

/* Add/remove a stem from the persisted set. st_mod_add does NOT load; the
 * caller loads first so a failure leaves the intent recorded (§7.6). */
int st_mod_add(uint64_t id, uint8_t plen, const char *name);
int st_mod_del(uint64_t id, uint8_t plen, const char *name);

/* The caller's position-derived region: pos_morton of the room's pos_t, then
 * xy_region_at. Deliberately NOT eng_map_mwhere -- that reinterprets the 8
 * pos_t bytes as a uint64 rather than Morton-encoding them (§22.4). */
int st_region_of_player(unsigned player_ref, uint64_t *id, uint8_t *plen);

extern unsigned sl_hd, owner_hd;

/* Legacy shift-spelling key, still spoken by the stchown/streload commands and
 * the transient `sl` handle table (§22.8). NOT the region table's key any more:
 * st_row_key above replaced it, because a CM_RECORD corm map requires
 * ktype == CM_STR and this is a packed binary struct (§22.1).
 *
 * Note st_key_new takes a POSITION and stores its PREFIX, and that `shift` is
 * the low-bit count, so a region at plen L has shift 64-L. The old st_open()
 * shifted an already-shifted value a second time (§2.4); nothing in the new
 * path re-shifts. */
static inline struct st_key
st_key_new(uint64_t key, unsigned shift) {
	struct st_key st_key;
	memset(&st_key, 0, sizeof(st_key));
	st_key.key = key >> shift;
	st_key.shift = shift;
	return st_key;
}

static inline int sthd_get(unsigned hd, void *value, uint64_t key, unsigned shift) {
	struct st_key st_key = st_key_new(key, shift);
	const void *__v = corm_get(hd, &st_key);
	if (__v) {
		memcpy(value, __v, corm_type_len(corm_get_vtype(hd)));
		return 1;
	}
	return 0;
}

static inline void sthd_put(unsigned hd, uint64_t key, unsigned shift, void *value) {
	struct st_key st_key = st_key_new(key, shift);
	corm_put(hd, &st_key, value);
}

void st_put(unsigned owner_ref, uint64_t key, unsigned shift);

#endif
