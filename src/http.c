#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

int nd_is_unmodded(void);
int nd_asset_dir(unsigned kind, char *out, size_t cap);

static const char nd_motd_default[] =
	"Welcome to Neverdark.\n\n"
	"Type `help` for the topic list, or `help begin` for a tour.\n"
	"To shape the world, load a game module or write your own:\n"
	"  help modules   installing and writing game modules\n"
	"  help loadmod   load a module\n"
	"  grant          hand authority to another player\n"
	"  sh             open a shell to edit and build code\n";

void
nd_bare_motd(socket_t fd)
{
	char dir[ENV_VALUE_LEN], path[ENV_VALUE_LEN], buf[BUFSIZ];
	const char *text = nd_motd_default;
	FILE *f = NULL;

	if (nd_asset_dir(ND_ASSET_MOTD, dir, sizeof(dir))) {
		snprintf(path, sizeof(path), "%s/motd", dir);
		f = fopen(path, "r");
	}
	if (!f)
		f = fopen("motd", "r");
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
	char dir[ENV_VALUE_LEN], path[ENV_VALUE_LEN];
	if (!nd_asset_dir(ND_ASSET_HTDOCS, dir, sizeof(dir)))
		snprintf(dir, sizeof(dir), "%s", AXIL_HTDOCS);
	snprintf(path, sizeof(path), "%s/%s", dir, file);
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

static void
nd_serve_art(socket_t fd, const char *rel)
{
	char dir[ENV_VALUE_LEN], path[ENV_VALUE_LEN];
	if (nd_asset_dir(ND_ASSET_ART, dir, sizeof(dir))) {
		snprintf(path, sizeof(path), "%s/%s", dir, rel);
		if (access(path, R_OK) == 0) {
			axil_sendfile(fd, path);
			return;
		}
	}
	axil_respond(fd, 404, "404 Not Found\n");
}

int
handle_nd_art(socket_t fd, char *body)
{
	(void)body;
	char uri[ENV_VALUE_LEN] = {0};
	if (axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI") != 0) {
		axil_respond(fd, 400, "Bad Request\n");
		return 1;
	}
	const char *prefix = "/nd/art/";
	size_t plen = strlen(prefix);
	if (strncmp(uri, prefix, plen) != 0) {
		axil_respond(fd, 404, "Not Found\n");
		return 1;
	}
	const char *rel = uri + plen;
	if (strstr(rel, "..") || *rel == '/' || *rel == '\0') {
		axil_respond(fd, 403, "Forbidden\n");
		return 1;
	}
	nd_serve_art(fd, rel);
	return 0;
}
