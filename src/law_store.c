#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/corm.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

static const char *
world_db(void)
{
	const char *db = getenv("AXIL_ND_DB");
	return db && *db ? db : "std.db";
}

#define MOD_TYPE_MAX 64
static const char *mod_type_name[MOD_TYPE_MAX];
static unsigned mod_type_tid[MOD_TYPE_MAX];
static unsigned mod_type_n;

void
mod_type_register(const char *name, unsigned tid)
{
	if (mod_type_n >= MOD_TYPE_MAX)
		return;
	mod_type_name[mod_type_n] = name;
	mod_type_tid[mod_type_n] = tid;
	mod_type_n++;
}

static unsigned
mod_type_lookup(const char *name)
{
	for (unsigned i = 0; i < mod_type_n; i++)
		if (!strcmp(mod_type_name[i], name))
			return mod_type_tid[i];
	return 0;
}

static uint32_t
shared_kind(const char *t)
{
	if (!strcmp(t, "s"))
		return CM_STR;
	if (!strcmp(t, "p"))
		return CM_PTR;
	return CM_U32;
}

#define HD_MOD_MAX 64
static unsigned hd_mod[HD_MOD_MAX];
static unsigned hd_mod_n;
static unsigned nd_hds[ND_HD_TABLE_MAX];

void
nd_store_set_hd(unsigned idx, unsigned val)
{
	if (idx < ND_HD_TABLE_MAX)
		nd_hds[idx] = val;
}

unsigned
nd_store_get_hd(unsigned idx)
{
	return (idx < ND_HD_TABLE_MAX) ? nd_hds[idx] : 0;
}

XY_IMPL(unsigned, nd_table_handle, unsigned, hd_idx)
{
	return nd_store_get_hd(hd_idx);
}

XY_IMPL(int, nd_table_handle_set, unsigned, hd_idx, unsigned, val)
{
	if (hd_idx < ND_HD_TABLE_MAX) {
		nd_store_set_hd(hd_idx, val);
		return 0;
	}
	return -1;
}

unsigned
hd_mod_open(const char *type, const char *iden, const char *anon, unsigned flags)
{
	if (hd_mod_n >= HD_MOD_MAX)
		return 0;

	unsigned vtype = mod_type_lookup(anon);
	if (!vtype)
		vtype = shared_kind(anon);
	unsigned h = corm_open(world_db(), type, shared_kind(iden),
		vtype, 0xFF, flags);
	if (!h)
		return 0;
	hd_mod[hd_mod_n] = h;
	return nd_hd_mod(hd_mod_n++);
}

unsigned
hd_resolve(unsigned hd)
{
	if (nd_hd_is_mod(hd)) {
		unsigned idx = nd_hd_mod_idx(hd);
		return idx < hd_mod_n ? hd_mod[idx] : 0;
	}
	return hd < ND_HD_TABLE_MAX ? nd_hds[hd] : 0;
}

XY_IMPL(int, nd_open, char *, type, char *, iden, char *, anon, unsigned, flags)
{
	return (int)hd_mod_open(type, iden, anon, flags);
}

XY_IMPL(unsigned, nd_put, unsigned, hd, void *, key, void *, data)
{
	return corm_put(hd_resolve(hd), key, data);
}

XY_IMPL(unsigned, nd_get, unsigned, hd, void *, value, void *, key)
{
	const void *v = corm_get(hd_resolve(hd), key);
	if (!v)
		return 1;
	memcpy(value, v, corm_len(corm_get_vtype(hd_resolve(hd)), v));
	return 0;
}

XY_IMPL(int, nd_del, unsigned, hd, void *, key)
{
	corm_del(hd_resolve(hd), key);
	return 0;
}

XY_IMPL(int, nd_del_value, unsigned, hd, void *, key, void *, value)
{
	return corm_del_value(hd_resolve(hd), key, value);
}

XY_IMPL(unsigned, nd_iter, unsigned, hd, void *, key)
{
	return corm_iter(hd_resolve(hd), key, 0);
}

XY_IMPL(int, nd_next, void *, key, void *, data, unsigned, cur)
{
	if (!corm_next_copy(key, data, cur)) {
		corm_fin(cur);
		return 0;
	}
	return 1;
}

XY_IMPL(int, nd_fin, unsigned, cur)
{
	corm_fin(cur);
	return 0;
}

XY_IMPL(int, nd_len_reg, char *, iden, size_t, len)
{
	unsigned tid = corm_reg(len);
	mod_type_register(iden, tid);
	return 0;
}

union fnptr {
	void (*fn)(void);
	void *ptr;
};

static void
shared_assoc_tramp(const void **skey, const void *pkey, const void *value, void *userdata)
{
	union fnptr u = { .ptr = userdata };
	nd_assoc_cb_t *fn = (nd_assoc_cb_t *)u.fn;
	fn(skey, pkey, value);
}

XY_IMPL(int, nd_assoc, unsigned, hd, unsigned, link, nd_assoc_cb_p, assoc)
{
	union fnptr u = { .fn = (void (*)(void))assoc };
	corm_assoc(hd_resolve(hd), hd_resolve(link), shared_assoc_tramp, u.ptr);
	return 0;
}
