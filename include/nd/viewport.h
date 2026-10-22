#ifndef ND_VIEWPORT_H
#define ND_VIEWPORT_H

/*
 * viewport.h — Pluggable Viewport Driver Interface for NeverDark.
 * Supports axil-tty, lamina 2D canvas, WebGL, and custom viewports.
 */

#include <stddef.h>
#include <stdint.h>
#include <bud/bud.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nd_viewport_driver {
	const char *id;                  /* "tty", "lamina-2d", "webgl" */
	const char *label;               /* "📺 TTY", "🎮 2D", "🧊 WebGL" */
	const char *required_capability; /* "axil-tty", "lamina", "webgl" */
	bud_node *(*mount)(void);        /* Renders the <pre>, <canvas>, etc. */
	int (*init)(void);               /* WASM-side initialization hook */
	void (*on_frame)(const uint8_t *buf, size_t len);
	void (*on_resize)(int w, int h);
	void (*destroy)(void);
} nd_viewport_driver_t;

#define ND_VIEWPORT_MAX_DRIVERS 8

/* Viewport Driver Registry API */
int nd_register_viewport_driver(const nd_viewport_driver_t *drv);
const nd_viewport_driver_t *nd_get_active_viewport_driver(void);
int nd_set_active_viewport_driver(const char *id);
int nd_viewport_driver_count(void);
const nd_viewport_driver_t *nd_get_viewport_driver_at(int idx);

#ifdef __cplusplus
}
#endif

#endif /* ND_VIEWPORT_H */
