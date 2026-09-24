#ifndef PLAYER_H
#define PLAYER_H

#include "uapi/object.h"
#include "config.h"

#include <string.h>
#include <ttypt/corm.h>

extern unsigned player_hd;

OBJ *player_connect(const char *qsession);
OBJ *player_create(char *name);

static inline unsigned
player_get(char *name)
{
	unsigned res = NOTHING;
	const void *__v = corm_get(player_hd, name);
	if (__v)
		res = *(const unsigned *)__v;
	return res;
}

static inline void
player_put(char *name, unsigned player_ref)
{
	corm_put(player_hd, name, &player_ref);
}

static inline void
player_delete(char *name)
{
	corm_del(player_hd, name);
}

#endif
