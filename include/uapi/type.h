#ifndef UAPI_TYPES_H
#define UAPI_TYPES_H

/* RECOMMENDATIONS:
 *
 * - Avoid passing entire objects in SIC calls. It's not very
 *   useful since getting / putting things by id can be fast.
 *   Only when you really don't have another way because you
 *   modify the object in the calling function and don't put
 *   and set around the SIC_CALL. Usually mods will use their
 *   custom object types, anyway.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "./object.h"
#include "./skel.h"
#include "./st.h"

/* Kept in lockstep with the module-facing copy in nd/xy-types.h, which is the
 * one modules actually see. Value = corm's CM_AINDEX: nd_open() forwards its
 * flags to corm_open(), so this must be the corm bit, not an engine-private
 * tag. It used to be 32, which was harmless only because hd_mod_open() dropped
 * the flags entirely. */
#define ND_AINDEX 1

/* Legacy SIC adapter descriptor. Once shared, then AX, now only read by the
 * residual dlopen path in src/mods.c; MODS.md Phase 3 deletes it with
 * `struct nd` and the whole sic_* surface. The game events below no longer use
 * it — they are libxylem hooks now. */
typedef struct {
	char name[64];
	size_t arg_size;
	size_t ret_size;
	int ran;
	void (*call)(void *, void *, void *);
	char ret[5096];
} sic_adapter_t;

#define STR(x) #x
#define XSTR(x) STR(x)

typedef void (*mod_cb_t)(void);

typedef struct {
	char str[256];
	int pos;
} sic_str_t;

typedef char small_buf_t[64];

typedef unsigned sic_areg_t(char *name, sic_adapter_t *adapter);
sic_areg_t sic_areg;

typedef void sic_call_t(void *retp, unsigned id, void *args);
sic_call_t sic_call;

typedef void sic_last_t(void *ret);
sic_last_t sic_last;

typedef unsigned sic_get_t(char *name);
sic_get_t sic_get;

/* --- game events --------------------------------------------------------- */
/* The engine fires these; game modules listen with XY_IMPL. Bodies are the
 * nd_evt_* wrappers in src/nd_events.c, which is the TU holding libaxil-nd's
 * injected module context — see that file for why the call sites go through
 * wrappers instead of XY_DECL.
 *
 * Signatures are the canonical ones from nd/hooks.h and must stay
 * ABI-identical to both it and the XY_DEFs. Return value is the last
 * listener's; 0 when no module implements the event. */
int nd_evt_status(unsigned player_ref);
int nd_evt_examine(unsigned player_ref, unsigned ref, unsigned type);
int nd_evt_add(unsigned ref, unsigned type, uint64_t v);
unsigned short nd_evt_view_flags(unsigned short flags, unsigned ref);
struct icon nd_evt_icon(unsigned ref, unsigned type, unsigned player_ref);
int nd_evt_del(unsigned ref, unsigned type);
int nd_evt_clone(unsigned orig_ref, unsigned nu_ref);
int nd_evt_update(unsigned ref, unsigned type, double dt);
int nd_evt_move(unsigned ref);

int nd_evt_vim(unsigned ref, sic_str_t ss);
struct bio nd_evt_noise(struct bio bio, uint32_t he, uint32_t w, uint32_t tm,
	uint32_t cl);
sic_str_t nd_evt_empty_tile(view_tile_t t, unsigned side, sic_str_t ss);

int nd_evt_new_player(unsigned player_ref);
int nd_evt_auth(unsigned player_ref);
int nd_evt_before_leave(unsigned ent_ref);
int nd_evt_leave(unsigned player_ref, unsigned loc_ref);
int nd_evt_enter(unsigned player_ref, unsigned loc_ref);
int nd_evt_after_enter(unsigned player_ref);
int nd_evt_spawn(unsigned player_ref, unsigned loc_ref, struct bio bio,
	uint64_t v);
int nd_evt_get(unsigned player_ref, unsigned ref);

extern unsigned type_hd, action_hd, vtf_hd, vtf_max;
extern unsigned situc_hd, sica_hd, sican_hd, bcp_hd, hd_hd, mod_hd, mod_id_hd;

typedef unsigned action_register_t(char *label, char *icon);
action_register_t eng_action_register;

typedef struct {
	struct print_info pi;
	char emp;
} vtf_t;

typedef struct action {
	char label[32], icon[32];
	unsigned flags;
} action_t;

typedef unsigned vtf_register_t(char emp, enum color fg, unsigned flags);
vtf_register_t eng_vtf_register;

#endif
