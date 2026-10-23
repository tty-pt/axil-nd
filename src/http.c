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

#include <bud/bud.h>

static void
nd_dynamic_fallback_ssr(socket_t fd)
{
	/* Pluggable viewport container with terminal */
	bud_node *term = bud_element("div");
	bud_set_attr(term, "id", "term");
	bud_set_attr(term, "data-bud-terminal", "1");
	bud_set_attr(term, "data-line-mode", "true");
	bud_set_attr(term, "class", "w-full h-full font-mono bg-[#0a0a0c] text-neutral-200 p-2 rounded border border-neutral-800 overflow-hidden");

	bud_node *vport = bud_element("div");
	bud_set_attr(vport, "id", "viewport");
	bud_set_attr(vport, "class", "flex-1 h-full min-h-[400px]");
	bud_append(vport, term);

	/* Compass keypad */
	bud_node *grid = bud_element("div");
	bud_set_attr(grid, "class", "grid grid-cols-3 gap-1 w-28 text-center select-none");
	bud_append(grid, bud_element("div"));
	bud_node *bk = bud_element("button"); bud_set_attr(bk, "type", "button"); bud_set_attr(bk, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bk, "title", "North (k)"); bud_append(bk, bud_text("↑")); bud_append(grid, bk);
	bud_node *bK = bud_element("button"); bud_set_attr(bK, "type", "button"); bud_set_attr(bK, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bK, "title", "Northeast (K)"); bud_append(bK, bud_text("↗")); bud_append(grid, bK);
	bud_node *bh = bud_element("button"); bud_set_attr(bh, "type", "button"); bud_set_attr(bh, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bh, "title", "West (h)"); bud_append(bh, bud_text("←")); bud_append(grid, bh);
	bud_append(grid, bud_element("div"));
	bud_node *bl = bud_element("button"); bud_set_attr(bl, "type", "button"); bud_set_attr(bl, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bl, "title", "East (l)"); bud_append(bl, bud_text("→")); bud_append(grid, bl);
	bud_append(grid, bud_element("div"));
	bud_node *bj = bud_element("button"); bud_set_attr(bj, "type", "button"); bud_set_attr(bj, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bj, "title", "South (j)"); bud_append(bj, bud_text("↓")); bud_append(grid, bj);
	bud_node *bJ = bud_element("button"); bud_set_attr(bJ, "type", "button"); bud_set_attr(bJ, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bJ, "title", "Southeast (J)"); bud_append(bJ, bud_text("↘")); bud_append(grid, bJ);

	/* Status gauges */
	bud_node *gauges = bud_tpl(
		"<div class='flex flex-col gap-2 w-full max-w-[180px]'>"
		"  <div class='flex flex-col gap-0.5'>"
		"    <div class='flex justify-between text-xs text-neutral-400'><span>HP</span><span id='hp-val'>100 / 100</span></div>"
		"    <div class='w-full bg-neutral-800 rounded h-2 overflow-hidden'><div id='hp-bar' class='bg-red-500 h-full rounded transition-all' style='width: 100%;'></div></div>"
		"  </div>"
		"  <div class='flex flex-col gap-0.5'>"
		"    <div class='flex justify-between text-xs text-neutral-400'><span>MP</span><span id='mp-val'>50 / 50</span></div>"
		"    <div class='w-full bg-neutral-800 rounded h-2 overflow-hidden'><div id='mp-bar' class='bg-blue-500 h-full rounded transition-all' style='width: 100%;'></div></div>"
		"  </div>"
		"</div>"
	);

	/* Actions dock */
	bud_node *actions = bud_element("div");
	bud_set_attr(actions, "id", "g-actions");
	bud_set_attr(actions, "class", "flex flex-wrap gap-1 items-center");
	bud_node *bi = bud_element("button"); bud_set_attr(bi, "type", "button"); bud_set_attr(bi, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bi, "title", "Inventory"); bud_append(bi, bud_text("📦")); bud_append(actions, bi);
	bud_node *bv = bud_element("button"); bud_set_attr(bv, "type", "button"); bud_set_attr(bv, "class", "btn btn-sm btn-ghost border border-neutral-700 hover:bg-neutral-800 text-neutral-200"); bud_set_attr(bv, "title", "Look"); bud_append(bv, bud_text("👁")); bud_append(actions, bv);

	/* Right panel */
	bud_node *top_group = bud_element("div");
	bud_set_attr(top_group, "class", "flex flex-col gap-4 items-center w-full");
	bud_append(top_group, grid);
	bud_append(top_group, gauges);

	bud_node *bot_group = bud_element("div");
	bud_set_attr(bot_group, "class", "w-full border-t border-neutral-800/60 pt-3");
	bud_append(bot_group, actions);

	bud_node *rpanel = bud_element("div");
	bud_set_attr(rpanel, "id", "right-panel");
	bud_set_attr(rpanel, "class", "flex flex-col gap-4 w-64 flex-shrink-0 bg-neutral-900/40 p-3 rounded-lg border border-neutral-800/80 items-center justify-between");
	bud_append(rpanel, top_group);
	bud_append(rpanel, bot_group);

	bud_node *inner = bud_element("div");
	bud_set_attr(inner, "class", "flex flex-1 gap-4 min-h-0");
	bud_append(inner, vport);
	bud_append(inner, rpanel);

	bud_node *root = bud_element("div");
	bud_set_attr(root, "id", "bud-root");
	bud_set_attr(root, "data-capabilities", "axil-ws tty lamina bud");
	bud_set_attr(root, "class", "flex flex-col w-full h-screen p-4 box-border gap-3");
	bud_append(root, inner);

	char *tree_html = bud_render_html(root);

	char page[BUFSIZ * 2];
	snprintf(page, sizeof(page),
		"<!DOCTYPE html>\n"
		"<html lang=\"en\">\n"
		"<head>\n"
		"  <meta charset=\"UTF-8\">\n"
		"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
		"  <title>NeverDark</title>\n"
		"  <link rel=\"stylesheet\" href=\"/styles.css\">\n"
		"  <script type=\"application/json\" id=\"bud-state\">"
		"{\"me\":0,\"hp\":{\"val\":100,\"max\":100},\"mp\":{\"val\":50,\"max\":50},"
		"\"room\":{\"ref\":1001,\"name\":\"The Citadel Common Hall\","
		"\"desc\":\"A grand hall with towering stone arches and banners.\",\"exits\":15},"
		"\"entities\":[]}"
		"</script>\n"
		"</head>\n"
		"<body data-modules=\"nd\" data-capabilities=\"axil-ws tty lamina bud\" class=\"bg-[#0a0a0c] text-neutral-200 min-h-screen m-0 p-0\">\n"
		"%s\n"
		"  <script type=\"module\" src=\"/bud-client.js\"></script>\n"
		"</body>\n"
		"</html>\n",
		tree_html ? tree_html : "");

	axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(fd, 200, page);

	if (tree_html)
		bud_free_string(tree_html);
	bud_free(root);
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
