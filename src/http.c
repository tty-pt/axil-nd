#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/axil.h>

#ifndef AXIL_HTDOCS
#define AXIL_HTDOCS "/usr/share/axil-nd/htdocs"
#endif

static void
nd_serve_htdocs(socket_t fd, const char *file)
{
	char path[ENV_VALUE_LEN];
	char htdocs[ENV_VALUE_LEN];
	const char *override = getenv("AXIL_ND_HTDOCS");

	if (!override || !*override)
		override = AXIL_HTDOCS;
	snprintf(htdocs, sizeof(htdocs), "%s", override);
	snprintf(path, sizeof(path), "%s/%s", htdocs, file);
	axil_sendfile(fd, path);
}

int
handle_nd(socket_t fd, char *body)
{
	(void)body;
	char key[ENV_VALUE_LEN] = {0};
	if (axil_env_get(fd, key, sizeof(key), "HTTP_SEC_WEBSOCKET_KEY") == 0) {
		if (axil_ws_upgrade(fd) < 0)
			return 1;
		return 0;
	}
	nd_serve_htdocs(fd, "index.html");
	return 0;
}
