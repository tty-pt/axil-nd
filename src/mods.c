/* mods.c — module-facing registration surface.
 *
 * Owns action_hd / vtf_hd / bcp_hd / hd_hd and the shared_* cursor helpers.
 * eng_nd_register / eng_action_register / eng_vtf_register are the
 * module-facing registration hooks.
 *
 * The old SIC adapter registry (sic_call / sic_last / sic_areg / sic_get /
 * sic_iter / sic_next / sic_put) and the sl_hd dlopen module loader
 * (mod_load / _mod_load / mod_load_all / mod_close, the module_id / mod
 * tables) are gone: libxylem is the dispatch, regions are the loader. One
 * bus, one lifecycle.
 */

#include <string.h>

#include <ttypt/axil.h>
#include <ttypt/corm.h>

#include "uapi/io.h"
#include "uapi/type.h"
#include "papi/nd.h"

unsigned vtf_hd, action_hd, bcp_hd, hd_hd;

unsigned shared_put(unsigned hd, void *key, void *data) {
	return corm_put(hd, key, data);
}

unsigned shared_get(unsigned hd, void *value, void *key) {
	const void *v = corm_get(hd, key);

	if (!v)
		return 1;
	/* Single lookup + corm_len, NOT corm_get_copy + a probe: that would be
	 * two hash lookups. corm_len sizes measured types from the value
	 * pointer; corm_type_len returns 0 for them (the zero-byte bug). */
	memcpy(value, v, corm_len(corm_get_vtype(hd), v));
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

	if (u.fn)
		((nd_assoc_cb_t *) u.fn)(skey, pkey, value);
}

void shared_assoc(unsigned hd, unsigned link, nd_assoc_cb_t assoc) {
	union fnptr u = { .fn = (void (*)(void)) assoc };

	corm_assoc(hd, link, shared_assoc_tramp, u.ptr);
}

/* §4.4: nd_assoc was declared (io.h) and advertised (nd-xy.h) but defined
 * nowhere, so any module calling it failed to link. It delegates to
 * shared_assoc; both names stay (the shared_* family stays coherent).
 *
 * Renamed nd_assoc -> eng_nd_assoc: the `nd_assoc` name now belongs to the
 * XY hook (XY_IMPL in nd_api.c), which emits a real function of that name --
 * the documented PROVIDER POLICY (see nd_api.c:9-11) that every hook's engine
 * body carries an `eng_` prefix so the C symbol differs from the hook name.
 * Only nd-race ever called it and it calls the hook, not this. */
void eng_nd_assoc(unsigned hd, unsigned link, nd_assoc_cb_t assoc) {
	shared_assoc(hd, link, assoc);
}

void eng_nd_register(char *str, nd_cb_t *cb, unsigned flags) {
	axil_register(str, cb, flags);
}

unsigned eng_action_register(char *label, char *icon) {
	action_t ai = { .flags = 0 };

	strlcpy(ai.label, label, sizeof(ai.label));
	strlcpy(ai.icon, icon, sizeof(ai.icon));
	return 1u << (corm_put(action_hd, NULL, &ai));
}

unsigned eng_vtf_register(char emp, enum color fg, unsigned flags) {
	vtf_t vtf = {
		.pi = { .fg = fg, .flags = flags },
		.emp = emp,
	};

	unsigned id = 1 << (corm_put(vtf_hd, NULL, &vtf));
	vtf_max = id;
	return id;
}
