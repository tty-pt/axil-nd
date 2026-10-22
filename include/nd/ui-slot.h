#ifndef ND_UI_SLOT_H
#define ND_UI_SLOT_H

/*
 * ui-slot.h — Caller-facing header for NeverDark UI slot hooks.
 * Implementers include <nd/ui-slot-types.h>, not this header.
 */

#include <ttypt/xy.h>
#include <nd/ui-slot-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Caller-facing UI hooks:
 * - on_nd_ui_action: Dispatched when an action dock button is activated.
 * - on_nd_ui_render_modal: Dispatched to render the active modal component.
 * - on_nd_ui_render_slot: Dispatched to render custom slot components (gauges, overlays).
 */
XY_DECL(int, on_nd_ui_action, const char *, action_id);
XY_DECL(bud_node *, on_nd_ui_render_modal, const char *, modal_id, const void *, state);
XY_DECL(bud_node *, on_nd_ui_render_slot, int, slot, const void *, state);
XY_DECL(int, on_nd_state_serialize, char *, buf, size_t, cap);

#ifdef __cplusplus
}
#endif

#endif /* ND_UI_SLOT_H */
