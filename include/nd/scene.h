#ifndef ND_SCENE_H
#define ND_SCENE_H

/*
 * scene.h — Canonical client Scene Graph operations and lifecycle API.
 */

#include "scene-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize scene graph with default room layout */
void nd_scene_init(nd_scene_t *scene);

/* Synchronize scene graph from latest BCP client state */
void nd_scene_sync(nd_scene_t *scene, const nd_client_state_t *st);

/* Helpers for module scene contributions (see nd_ui_glue_t.scene_sync). */
nd_scene_entity_t *nd_scene_find(nd_scene_t *scene, uint32_t ref);
void nd_scene_attach(nd_scene_entity_t *e, const char *socket, uint32_t ref);
void nd_scene_fx_add(nd_scene_t *scene, const char *kind, uint32_t target_ref, uint32_t serial);

#ifdef __cplusplus
}
#endif

#endif /* ND_SCENE_H */
