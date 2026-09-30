#ifndef UAPI_MAP_H
#define UAPI_MAP_H

#include "./st.h"

typedef int map_has_t(unsigned thing);
map_has_t eng_map_has;

typedef morton_t map_mwhere_t(unsigned thing);
map_mwhere_t eng_map_mwhere;

typedef void map_where_t(pos_t p, unsigned thing);
map_where_t eng_map_where;

typedef int map_delete_t(unsigned what);
map_delete_t eng_map_delete;

typedef unsigned map_get_t(pos_t at);
map_get_t eng_map_get;

#endif
