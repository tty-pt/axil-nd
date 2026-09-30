#include "uapi/map.h"

#include <string.h>

#include <ttypt/corm.h>
#include <ttypt/islet.h>

#include "config.h"
#include "st.h"

typedef struct {
	unsigned what;
	morton_t where;
} map_range_t;

/* The multivalue u64→u32 islet map: position → thing ref.  The side index
 * "map_w" (u32 → pos_t) gives the reverse: thing ref → its stored pos bytes,
 * so eng_map_mwhere()/eng_map_where()/eng_map_delete() avoid a box scan. Both share
 * the world db path (world_db(): AXIL_ND_DB else STD_DB); corm keyed dbs by
 * XXH32(database) within one file. */

static uint32_t map_hd, w_hd, pos_type;

void
map_init(void)
{
	islet_init();
	map_hd = islet_open(world_db(), "map", 0xFFFFFF);
	pos_type = corm_reg(sizeof(pos_t));
	w_hd = corm_open(world_db(), "map_w", CM_U32, pos_type, 0xFFFF, 0);
}

int
map_close(unsigned flags)
{
	(void) flags;
	corm_close(map_hd);
	corm_close(w_hd);
	return 0;
}

int
map_sync(void)
{
	corm_save();
	return 0;
}

void
map_put(pos_t p, unsigned thing, int flags)
{
	if (flags && islet_get_4(map_hd, p) != ISLET_MISS)
		return;
	islet_set_4(map_hd, p, thing);
	corm_put(w_hd, &thing, p);
}

// see http://www.vision-tools.com/h-tropf/multidimensionalrangequery.pdf

static inline int
map_range_unsafe(map_range_t *res,
		     size_t n,
		     struct rect4D *rect)
{
	uint16_t l[4];
	int i;
	uint32_t cur;
	int16_t p[4];
	uint32_t ref;
	map_range_t *r = res;
	size_t nn = n;

	/* islet_iter_4's box is INCLUSIVE (covers s[i]..s[i]+l[i]); ND's
	 * rect upper bound was exclusive.  Convert: 7×7 l={7,7,1,1} →
	 * 6,6,0,0 → still 7×7=49, mat[49] never overflows. */
	for (i = 0; i < 4; i++) {
		if (rect->l[i] == 0)
			return 0;
		l[i] = rect->l[i] - 1;
	}

	cur = islet_iter_4(map_hd, (int16_t *) rect->s, l);

	while (nn && islet_next(p, &ref, cur)) {
		r->what = ref;
		memcpy(&r->where, p, sizeof(pos_t));
		r++;
		nn--;
	}
	/* islet_next() auto-frees the iterator on 0; drain to 0 (box ≤7×7). */
	while (islet_next(p, &ref, cur)) ;

	return r - res;
}

/* this version accounts for type limits,
 * and wraps around them in each direction, if necessary
 * TODO iterative form
 */

static int
map_range_safe(map_range_t *res,
		   size_t n,
		   struct rect4D *rect,
		   int dim)
{
	map_range_t *re = res;
	int i, aux;
	size_t nn = n;
	struct rect4D r = *rect;

	for (i = dim; i < DIM; i++) {
		if (rect->s[i] + rect->l[i] <= COORD_MAX)
			continue;

		r.l[i] = COORD_MAX - rect->s[i];
		r.s[i] = rect->s[i];
		aux = map_range_safe(re, nn, &r, i + 1);

		if (aux < 0)
			return -1;

		nn -= aux;
		re += aux;

		r.l[i] = rect->s[i] + rect->l[i] - COORD_MAX - 1;
		r.s[i] = COORD_MIN + 1;
		aux = map_range_safe(re, nn, &r, i + 1); 

		if (aux < 0)
			return -1;

		nn -= aux;

		return n - nn;
	}

	return map_range_unsafe(re, nn, rect);
}

void
map_search(unsigned *mat, pos_t pos, unsigned radius)
{
	const unsigned side = 2 * radius + 1;
	const unsigned m = side * side;

	struct rect4D vpr = {
		{ pos[0] - radius, pos[1] - radius, pos[2], pos[3] },
		{ side, side, 1, 1 },
	};

	/* static const size_t m = 49; */
	map_range_t buf[m];
	size_t n, i;

	n =  map_range_safe(buf, m, &vpr, 0);
	memset(mat, -1, sizeof(unsigned) * m);

	for (i = 0; i < n; i++) {
		pos_t p;
		memcpy(p, &buf[i].where, sizeof(pos_t));
		mat[point_rel_idx(p, vpr.s, side)] = buf[i].what;
	}
}

morton_t
eng_map_mwhere(unsigned where)
{
	unsigned room = where;
	const void *v = corm_get(w_hd, &room);

	if (!v) {
		static morton_t code = 130056652770671ULL;
		return code;
	}

	return * (const morton_t *) v;
}

int
eng_map_has(unsigned room) {
	return eng_map_mwhere(room) != 130056652770671ULL;
}

void
eng_map_where(pos_t p, unsigned room)
{
	unsigned r = room;
	const void *v = corm_get(w_hd, &r);

	if (v)
		memcpy(p, v, sizeof(pos_t));
	else
		memset(p, 0, sizeof(pos_t)); /* unmapped room: void/limbo coords */
}

unsigned
eng_map_get(pos_t p)
{
	return islet_get_4(map_hd, p);
}

int
eng_map_delete(unsigned what)
{
	unsigned x = what;
	const void *v = corm_get(w_hd, &x);

	if (!v)
		return -1;

	pos_t p;

	memcpy(p, v, sizeof(pos_t));
	islet_del_value_4(map_hd, p, what);
	corm_del(w_hd, &x);

	return 0;
}