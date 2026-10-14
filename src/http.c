#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/axil.h>
#include <ttypt/xy.h>

int nd_is_unmodded(void);

static const char nd_motd_default[] =
	"Welcome to Neverdark.\n"
	"\n"
	"Type `help` for the topic list, or `help begin` for a tour.\n"
	"To shape the world, load a game module or write your own:\n"
	"  help modules   installing and writing game modules\n"
	"  help loadmod   load a module\n"
	"  grant          hand authority to another player\n"
	"  sh             open a shell to edit and build code\n";

void
nd_bare_motd(socket_t fd)
{
	char buf[BUFSIZ];
	FILE *f = fopen("motd", "r");
	const char *text = nd_motd_default;

	if (f) {
		size_t len = fread(buf, 1, sizeof(buf) - 2, f);
		fclose(f);
		if (len > 0) {
			if (buf[len - 1] != '\n')
				buf[len++] = '\n';
			buf[len] = '\0';
			text = buf;
		}
	}
	axil_write(fd, (void *)text, strlen(text));
}

int
axil_accept(socket_t fd)
{
	if (nd_is_unmodded())
		nd_bare_motd(fd);
	return 0;
}

XY_IMPL(int, on_axil_connect, socket_t, fd)
{
	if (nd_is_unmodded())
		nd_bare_motd(fd);
	return 0;
}

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
