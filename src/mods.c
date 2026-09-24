/* mods.c — J4.8 step 4: SIC adapter registry + content-module loader, split
 * out of x/interface.c.  libxylem is the new sic: the SIC adapter machinery
 * maps onto XY hooks; the registries stay corm maps (J4.5 #3).
 *
 * Owns sica_hd / sican_hd / situc_hd / action_hd / vtf_hd / bcp_hd / hd_hd /
 * mod_hd / mod_id_hd and the whole sic/mod surface.  eng_nd_register /
 * eng_action_register / eng_vtf_register are the module-facing registration hooks
 * (their XY providers will sit here too in Phase 2, renamed to dodge the
 * hook names).
 */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <ttypt/axil.h>
#include <ttypt/corm.h>
#include <ttypt/qsys.h>

#include "uapi/io.h"
#include "uapi/type.h"
#include "papi/nd.h"

unsigned sica_hd, sican_hd, situc_hd, vtf_hd, action_hd, bcp_hd, hd_hd;
unsigned mod_hd, mod_id_hd;

void sic_last(void *ret) {
	if (!nd.adapter || !nd.adapter->ran)
		return;
	memcpy(ret, nd.adapter->ret, nd.adapter->ret_size);
}

void sic_call(void *retp, unsigned id, void *arg) {
	const void *av = corm_get(sica_hd, &id);
	sic_adapter_t adapter;
	const void *kp, *vp;
	unsigned c;

	if (!av) {
		fprintf(stderr, "No adapter registered for symbol id '%u'\n", id);
		return;
	}
	memcpy(&adapter, av, sizeof(adapter));
	adapter.ran = 0;

	c = corm_iter(mod_id_hd, NULL, 0);
	while (corm_next(&kp, &vp, c)) {
		void *sl = *(void * const *)vp;
		void *cb = dlsym(sl, adapter.name);

		if (!cb)
			continue;

		struct nd *ndr = (void *) dlsym(sl, "nd");
		if (ndr)
			ndr->adapter = &adapter;
		adapter.call(retp, cb, arg);
		adapter.ran++;

		memcpy(adapter.ret, retp, adapter.ret_size);
	}
	corm_fin(c);
}

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
 * shared_assoc; both names stay (the shared_* family stays coherent). */
void nd_assoc(unsigned hd, unsigned link, nd_assoc_cb_t assoc) {
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

unsigned sic_iter(unsigned si_id, unsigned type) {
	static unsigned c;
	unsigned key[2] = { si_id, type };
	c = corm_iter(situc_hd, key, 0);
	return c;
}

int sic_next(void **cb, unsigned c) {
	const void *kp, *vp;

	if (!corm_next(&kp, &vp, c)) {
		corm_fin(c);
		return 0;
	}
	*cb = *(void * const *)vp;
	return 1;
}

void sic_put(unsigned si_id, unsigned type, void *cb) {
	unsigned key[2] = { si_id, type };
	corm_put(situc_hd, key, &cb);
}

unsigned on_status_id, on_examine_id, on_add_id,
	 on_view_flags_id, on_icon_id, on_del_id, on_clone_id,
	 on_update_id, on_move_id, on_vim_id, on_new_player_id,
	 on_auth_id, on_before_leave_id, on_leave_id,
	 on_enter_id, on_after_enter_id, on_spawn_id,
	 on_get_id, on_noise_id, on_empty_tile_id;

SIC_DEF(int, on_status, unsigned, player_ref);
SIC_DEF(int, on_examine, unsigned, player_ref, unsigned, ref, unsigned, type);
SIC_DEF(int, on_add, unsigned, ref, unsigned, type, uint64_t, v);
SIC_DEF(unsigned short, on_view_flags, unsigned short, flags, unsigned, ref);
SIC_DEF(struct icon, on_icon, unsigned, ref, unsigned, type, unsigned, player_ref);
SIC_DEF(int, on_del, unsigned, ref, unsigned, type);
SIC_DEF(int, on_clone, unsigned, orig_ref, unsigned, nu_ref);
SIC_DEF(int, on_update, unsigned, ref, unsigned, type, double, dt);
SIC_DEF(int, on_move, unsigned, ref);

SIC_DEF(int, on_vim, unsigned, ref, sic_str_t, ss);

SIC_DEF(int, on_new_player, unsigned, player_ref);
SIC_DEF(int, on_auth, unsigned, player_ref);
SIC_DEF(int, on_before_leave, unsigned, ent_ref);
SIC_DEF(int, on_leave, unsigned, player_ref, unsigned, loc_ref);
SIC_DEF(int, on_enter, unsigned, player_ref, unsigned, loc_ref);
SIC_DEF(int, on_after_enter, unsigned, player_ref);
SIC_DEF(int, on_spawn, unsigned, player_ref, unsigned, loc_ref, struct bio, bio, uint64_t, v);
SIC_DEF(int, on_get, unsigned, player_ref, unsigned, ref);

SIC_DEF(struct bio, on_noise, struct bio, bio, uint32_t, he, uint32_t, w, uint32_t, tm, uint32_t, cl);
SIC_DEF(sic_str_t, on_empty_tile, view_tile_t, t, unsigned, side, sic_str_t, ss);

unsigned sic_areg(char *name, sic_adapter_t *adapter) {
	unsigned id = corm_put(sica_hd, NULL, adapter);
	corm_put(sican_hd, name, &id);
	return id;
}

unsigned sic_get(char *name) {
	unsigned ret = NOTHING;
	const void *v = corm_get(sican_hd, name);

	if (v)
		ret = *(const unsigned *)v;
	return ret;
}

void mod_close(void) {
	const void *kp, *vp;
	unsigned c = corm_iter(mod_id_hd, NULL, 0);

	while (corm_next(&kp, &vp, c))
		dlclose(*(void * const *)vp);
	corm_fin(c);
}

int _mod_run(void *sl, char *symbol) {
	mod_cb_t cb = (mod_cb_t) dlsym(sl, symbol);

	if (!cb) {
		fprintf(stderr, "couldn't find %s\n", symbol);
		return 1;
	}
	cb();
	return 0;
}

void _mod_load(char *fname) {
	void *sl;

	sl = dlopen(fname, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);

	if (!sl) {
		fprintf(stderr, "_mod_load failed loading '%s': %s\n", fname, dlerror());
		return;
	}

	/* libnd versions and updating rules: mod_auto_init + mod_install on a
	 * fresh module, mod_open (re-run) when the module id is already known */
	unsigned existed = corm_get(mod_hd, fname) != NULL;
	struct nd *ind = dlsym(sl, "nd");
	if (ind)
		*ind = nd;

	char *symbol = existed ? "mod_open" : "mod_install";

	WARN("%s: '%s'\n", symbol, fname);
	unsigned id = corm_put(mod_id_hd, NULL, &sl);
	corm_put(mod_hd, fname, &id);

	mod_cb_t auto_init = (mod_cb_t) dlsym(sl, "mod_auto_init");
	if (auto_init)
		auto_init();

	_mod_run(sl, symbol);
}

void mod_load(char *fname) {
	if (corm_get(mod_hd, fname)) {
		WARN("module '%s' already present\n", fname);
		return;
	}

	_mod_load(fname);
}

void mod_load_all(void) {
	const void *kp, *vp;
	unsigned c = corm_iter(mod_id_hd, NULL, 0);
	char key[16];

	WARN("Existed! Loading all modules\n");
	while (corm_next(&kp, &vp, c)) {
		snprintf(key, sizeof(key), "%u", *(const unsigned *)kp);
		_mod_load(key);
	}
	corm_fin(c);
}