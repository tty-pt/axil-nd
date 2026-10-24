#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

int nd_is_unmodded(void);
int nd_asset_dir(unsigned kind, char *out, size_t cap);

XY_DECL(int, nd_ssr_respond, socket_t, fd);

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

static const char nd_fallback_page_html[] =
	"<!DOCTYPE html>\n"
	"<html lang=\"en\">\n"
	"<head>\n"
	"  <meta charset=\"UTF-8\">\n"
	"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
	"  <title>NeverDark</title>\n"
	"  <link rel=\"stylesheet\" href=\"/styles.css\">\n"
	"  <script type=\"application/json\" id=\"bud-state\">"
	"{\"me\":0,\"gauges\":[],\"room\":{\"ref\":0,\"name\":\"\",\"desc\":\"\",\"exits\":0},"
	"\"entities\":[]}"
	"</script>\n"
	"</head>\n"
	"<body data-modules=\"nd\" data-capabilities=\"axil-ws axil-tty lamina bud webgl xylem\" class=\"bg-[#0a0a0c] text-neutral-200 min-h-screen m-0 p-0\">\n"
	"<div id=\"bud-root\" data-capabilities=\"axil-ws axil-tty lamina bud webgl xylem\" class=\"flex flex-col w-full h-screen p-4 box-border gap-3\">"
	"<div class=\"flex flex-1 gap-4 min-h-0\">"
	"<div id=\"viewport\" class=\"flex-1 h-full min-h-[400px]\">"
	"<div id=\"term\" data-bud-terminal=\"1\" data-line-mode=\"true\" class=\"w-full h-full font-mono bg-[#0a0a0c] text-neutral-200 p-2 rounded border border-neutral-800 overflow-hidden\"></div>"
	"</div>"
	"<div id=\"right-panel\" class=\"flex flex-col gap-4 w-64 flex-shrink-0 bg-neutral-900/40 p-3 rounded-lg border border-neutral-800/80 items-center justify-between\">"
	"<div class=\"flex flex-col gap-4 items-center w-full\">"
	"<div class=\"grid grid-cols-3 gap-1 w-28 text-center select-none\">"
	"<div></div>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"North (k)\">↑</button>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"Northeast (K)\">↗</button>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"West (h)\">←</button>"
	"<div></div>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"East (l)\">→</button>"
	"<div></div>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"South (j)\">↓</button>"
	"<button type=\"button\" class=\"btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200\" title=\"Southeast (J)\">↘</button>"
	"</div>"
	"</div>"
	"<div class=\"w-full border-t border-neutral-800/60 pt-3\"></div>"
	"</div>"
	"</div>"
	"</div>\n"
	"  <script type=\"module\" src=\"/bud-client.js\"></script>\n"
	"</body>\n"
	"</html>\n";

static void
nd_dynamic_fallback_ssr(socket_t fd)
{
	axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(fd, 200, nd_fallback_page_html);
}

#ifndef AXIL_HTDOCS
#define AXIL_HTDOCS "/usr/share/axil-nd/htdocs"
#endif

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

	char uri[ENV_VALUE_LEN] = {0};
	axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");

	/* If requesting /nd.wasm or sub-asset under /nd/ (e.g. /nd/app.css) */
	if (uri[0] && (strcmp(uri, "/nd.wasm") == 0 || (strncmp(uri, "/nd/", 4) == 0 && uri[4] != '\0'))) {
		const char *file = (strcmp(uri, "/nd.wasm") == 0) ? "nd.wasm" : (uri + 4);
		if (strstr(file, "..") || *file == '/') {
			axil_respond(fd, 403, "Forbidden\n");
			return 1;
		}
		char dir[ENV_VALUE_LEN], path[ENV_VALUE_LEN];
		if (!nd_asset_dir(ND_ASSET_HTDOCS, dir, sizeof(dir)))
			snprintf(dir, sizeof(dir), "%s", AXIL_HTDOCS);
		snprintf(path, sizeof(path), "%s/%s", dir, file);
		if (access(path, R_OK) == 0) {
			axil_sendfile(fd, path);
			return 0;
		}
		axil_respond(fd, 404, "404 Not Found\n");
		return 1;
	}

	/* SSR entrypoint for /nd and /nd/ */
	int rc = -1;
	struct nd_ssr_respond_args args = { fd };
	if (xy_call(&rc, &__xy_decl_nd_ssr_respond_adapter, &args) == XY_OK && rc == 0)
		return 0;

	nd_dynamic_fallback_ssr(fd);
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
