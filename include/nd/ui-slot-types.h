#ifndef ND_UI_SLOT_TYPES_H
#define ND_UI_SLOT_TYPES_H

/*
 * ui-slot-types.h — Extensible UI slot types and enums for NeverDark.
 * Two-header separation rule: contains ZERO XY_DECLs so implementers can include it.
 */

#include <stddef.h>
#include <stdint.h>
#include <bud/bud.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ND_UI_SLOT_VIEWPORT,       /* Primary visual container */
	ND_UI_SLOT_GAUGES,         /* Status bars / meters */
	ND_UI_SLOT_ACTIONS,        /* Action dock buttons in #g-actions */
	ND_UI_SLOT_MODALS,         /* Modal overlays / popups */
	ND_UI_SLOT_HUD_OVERLAYS,   /* Floating widgets over the viewport */
	ND_UI_SLOT_MAX
} nd_ui_slot_t;

typedef bud_node *(*nd_ui_slot_render_fn)(const void *state);
typedef int (*nd_ui_action_handler_fn)(bud_event *ev, const char *action_id);

typedef struct nd_ui_action_button {
	const char *id;             /* module-defined action id */
	const char *icon;           /* e.g. action icon */
	const char *title;          /* Tooltip */
	int priority;               /* Ordering in dock: lower = earlier */
	nd_ui_action_handler_fn handler;
} nd_ui_action_button_t;

typedef struct nd_ui_modal_desc {
	const char *id;             /* module-defined modal id */
	nd_ui_slot_render_fn render;
} nd_ui_modal_desc_t;

typedef struct nd_ui_extension {
	const char *module_name;    /* owning tenant module name */
	nd_ui_slot_t slot;
	const char *slot_id;        /* identifier for modal or component */
	nd_ui_slot_render_fn render;
	nd_ui_action_handler_fn handle_action;
	struct nd_ui_extension *next;
} nd_ui_extension_t;

#define ND_UI_MAX_ACTIONS 16
#define ND_UI_MAX_MODALS  16

/* Active modal state */
void nd_ui_set_active_modal(const char *modal_id);
/* Send a command line to the server over the active transport (WASM-only effect). */
int nd_ui_send_cmd(const char *cmd);
const char *nd_ui_get_active_modal(void);
/* Open the named modal, or close it if it is already the active one. */
void nd_ui_toggle_modal(const char *modal_id);
/* Build a dock button that dispatches on_nd_ui_action(action_id) on click.
 * Tenant modules call this from their ND_UI_SLOT_ACTIONS contribution. */
bud_node *nd_ui_dock_button(const char *icon, const char *title, const char *action_id);

/*
 * UI glue registry. Tenant modules register one nd_ui_glue_t from a static
 * constructor (ND_UI_GLUE); the shell fans out to every registered glue.
 * Unlike xy listeners this works identically for the native SSR build and the
 * WASM reactor, so SSR and hydration render the same module contributions.
 * Any member may be NULL.
 */
typedef struct nd_ui_glue {
	/* Append own nodes to `parent` (a bud_node *) for an nd_ui_slot_t. */
	int (*contribute)(int slot, void *parent, const void *state);
	/* Return a node for the slot, or NULL to pass (first non-NULL wins). */
	bud_node *(*render_slot)(int slot, const void *state);
	/* Seed module-owned defaults into a fresh nd_client_state_t. */
	int (*state_default)(void *state);
	/* Append `"key":{...}` fragments (comma-separated) to buf. */
	int (*state_serialize)(const void *state, char *buf, size_t cap);
	/* Attach module-owned things to the presentation-neutral scene graph
	 * (`scene` is the host's scene object). Runs after every state sync. */
	void (*scene_sync)(void *scene, const void *state);
} nd_ui_glue_t;

void nd_ui_glue_register(const nd_ui_glue_t *glue);
void nd_ui_glue_contribute(int slot, void *parent, const void *state);
bud_node *nd_ui_glue_render_slot(int slot, const void *state);
void nd_ui_glue_state_default(void *state);
void nd_ui_glue_state_serialize(const void *state, char *buf, size_t cap);
void nd_ui_glue_scene_sync(void *scene, const void *state);

#define ND_UI_GLUE(tag, ...) \
	static const nd_ui_glue_t nd_ui_glue_##tag = { __VA_ARGS__ }; \
	static void __attribute__((constructor)) nd_ui_glue_reg_##tag(void) \
	{ nd_ui_glue_register(&nd_ui_glue_##tag); }

#ifdef __cplusplus
}
#endif

#endif /* ND_UI_SLOT_TYPES_H */
