#include <stdio.h>
#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>

int handle_nd(socket_t fd, char *body);
int handle_nd_art(socket_t fd, char *body);

XY_MODULE_API void
xy_install(void)
{
	axil_register_handler("GET:/nd", handle_nd);
	axil_register_handler("GET:/nd/art/*", handle_nd_art);
}
