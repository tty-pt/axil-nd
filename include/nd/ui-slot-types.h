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
	ND_UI_SLOT_GAUGES,         /* Status bars (HP, MP, Stamina, etc.) */
	ND_UI_SLOT_ACTIONS,        /* Action dock buttons in #g-actions */
	ND_UI_SLOT_MODALS,         /* Modal overlays / popups */
	ND_UI_SLOT_HUD_OVERLAYS,   /* Floating widgets over the viewport */
	ND_UI_SLOT_MAX
} nd_ui_slot_t;

typedef bud_node *(*nd_ui_slot_render_fn)(const void *state);
typedef int (*nd_ui_action_handler_fn)(bud_event *ev, const char *action_id);

typedef struct nd_ui_action_button {
	const char *id;             /* e.g. "equip", "stats", "spells" */
	const char *icon;           /* e.g. "👕", "📋", "✨" */
	const char *title;          /* Tooltip */
	int priority;               /* Ordering in dock: lower = earlier */
	nd_ui_action_handler_fn handler;
} nd_ui_action_button_t;

typedef struct nd_ui_modal_desc {
	const char *id;             /* e.g. "stats", "equip" */
	nd_ui_slot_render_fn render;
} nd_ui_modal_desc_t;

typedef struct nd_ui_extension {
	const char *module_name;    /* "nd-equip", "nd-attr", etc. */
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
const char *nd_ui_get_active_modal(void);

#ifdef __cplusplus
}
#endif

#endif /* ND_UI_SLOT_TYPES_H */
