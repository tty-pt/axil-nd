#include <stdio.h>
#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>

int handle_nd(socket_t fd, char *body);
int handle_nd_art(socket_t fd, char *body);

XY_MODULE_API void
xy_install(void)
{
	xy_load("libnd-region");
	xy_load("libnd-bus");
	xy_load("libnd-obj");
	xy_load("libnd-world");
	xy_load("libnd-view");
	xy_load("libnd-mcp");
	xy_load("libnd-speech");
	xy_load("libnd-session");

	axil_register_handler("GET:/nd", handle_nd);
	axil_register_handler("GET:/nd/*", handle_nd);
	axil_register_handler("GET:/nd/art/*", handle_nd_art);
	axil_register_handler("GET:/nd.wasm", handle_nd);
}
