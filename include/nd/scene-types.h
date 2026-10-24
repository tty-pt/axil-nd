#ifndef ND_SCENE_TYPES_H
#define ND_SCENE_TYPES_H

/*
 * scene-types.h — Canonical reactive client Scene Graph types for 2D (Lamina) and 3D (WebGL) viewports.
 * Two-header separation rule: pure types and constants, zero implementations.
 */

#include <stddef.h>
#include <stdint.h>
#include <nd/bcp-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ND_SCENE_TILE_FLOOR    0
#define ND_SCENE_TILE_WALL     1
#define ND_SCENE_TILE_DOOR     2
#define ND_SCENE_TILE_WATER    3
#define ND_SCENE_TILE_GRASS    4

typedef struct {
	float x, y, z;
} nd_vec3_t;

#define ND_SCENE_MAX_ATTACH 12
#define ND_SCENE_MAX_FX     8

/* Something attached to an entity at a named socket (a skeleton bone for 3D,
 * a pip for 2D, a table row for text). Modules fill these in; the scene does
 * not know what they mean. */
typedef struct {
	char socket[16];
	uint32_t ref;
} nd_scene_attach_t;

/* A one-shot visual effect (e.g. an elemental burst) aimed at an entity. `serial`
 * identifies the originating event so it is recorded once. */
typedef struct {
	char kind[16];
	uint32_t target_ref;
	uint32_t serial;
} nd_scene_fx_t;

typedef struct {
	uint32_t ref;
	char name[48];
	char ch;
	uint16_t fg;
	nd_vec3_t pos;
	nd_vec3_t target_pos;
	uint16_t hp_val;
	uint16_t hp_max;
	uint8_t is_player;
	uint8_t is_target;
	uint8_t is_hostile;
	nd_scene_attach_t attach[ND_SCENE_MAX_ATTACH];
	size_t attach_count;
} nd_scene_entity_t;

typedef struct {
	uint32_t ref;
	char name[64];
	uint32_t exits; /* Bitmask: N=1, S=2, E=4, W=8, U=16, D=32 */
	uint16_t width;  /* Grid columns */
	uint16_t height; /* Grid rows */
	uint8_t tiles[32 * 32];
} nd_scene_room_t;

typedef struct {
	nd_scene_room_t room;
	nd_scene_entity_t entities[ND_MAX_ENTITIES];
	size_t entity_count;
	uint32_t player_ref;
	uint32_t target_ref;
	nd_scene_fx_t fx[ND_SCENE_MAX_FX];
	size_t fx_count;
	float time_of_day; /* 0.0 (midnight) -> 0.5 (noon) -> 1.0 (midnight) */
	struct {
		nd_vec3_t pos;
		float pitch;
		float yaw;
		float zoom;
	} camera;
} nd_scene_t;

#ifdef __cplusplus
}
#endif

#endif /* ND_SCENE_TYPES_H */
