#ifndef _CONFIG_H
#define _CONFIG_H

#define BUFFER_LEN 8192
#define STD_DB "/var/nd/std.db"

extern int euid;

/* engine boot's store path (AXIL_ND_DB override else STD_DB); defined in
 * world.c, used by map.c + spacetime.c so the real-engine boot works
 * in-tree. */
const char *world_db(void);

#endif
