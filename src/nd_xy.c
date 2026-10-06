/* nd_xy.c — the NEVERDARK engine's libxylem provider side.
 *
 * This TU XYZ_IMPLs the game API hooks declared for game modules in
 * nd/xy.h. It is the ONE TU that provides io primitives: game modules
 * (loaded via xy_load into the engine's region subtree) call `nd_write(player,
 * str, len)` as plain C; XY_CALL dispatches here.
 *
 * Like mods/common/common.c, this TU must NOT include nd/xy.h
 * (XY_IMPL + XY_DECL on the same symbol clash). Game modules include it.
 *
 * The fd→player registry (dplayer_hd/fds_hd) and the write-path bodies now
 * live in the real engine (src/io.c, `eng_*` names so the C symbols differ
 * from these hook names); this TU only adapts them to the XY signatures.
 */

/* This file is #included into src/libaxil-nd.c (single-TU module). Do NOT
 * compile it as a separate translation unit and do NOT #include ttypt/xy-mod.h
 * here: the module's single `static struct xy_ctx xy` (and the weak
 * get_xy_ptr() the host injects the context into) lives in libaxil-nd.c. */

#include <ttypt/corm.h>
#include <ttypt/axil.h>

#include <errno.h>
#include <libgen.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* engine registry + providers (io.c). uapi/io.h is NOT includable here
 * (its azoth/object defs collide with the value types below), hence the
 * local externs. */
extern unsigned fds_hd;
extern unsigned eng_fd_player(unsigned fd);
extern void eng_nd_write(unsigned player_ref, char *str, size_t len);
extern void eng_nd_rwrite(unsigned room_ref, unsigned exception_ref,
	char *str, size_t len);
extern void eng_nd_wwrite(unsigned player_ref, void *msg, size_t len);
extern void eng_nd_twrites(unsigned player_ref, char *str, size_t len);
extern void eng_nd_close(unsigned player_ref);
extern void eng_nd_flush(unsigned player_ref);

/* --- XY_IMPL providers for the nd/xy.h hooks ------------------------- */

/* forward decls (XY_IMPL emits bodies below; keep TU-internal callers happy) */
extern unsigned fd_player(unsigned fd);
extern int nd_write(unsigned player_ref, char *str, size_t len);
extern int nd_twrites(unsigned player_ref, char *str, size_t len);

XY_IMPL(unsigned, fd_player, unsigned, fd)
{
	return eng_fd_player(fd);
}

XY_IMPL(int, nd_write, unsigned, player_ref, char *, str, size_t, len)
{
	uint32_t cur = corm_get_multi(fds_hd, &player_ref);
	const void *kp, *vp;
	int n = 0;

	if (cur == CM_MISS)
		return 0;
	while (corm_next(&kp, &vp, cur))
		n++;
	corm_fin(cur);
	eng_nd_write(player_ref, str, len);
	return n;
}

XY_IMPL(int, nd_rwrite, unsigned, room_ref, unsigned, exception_ref,
	char *, str, size_t, len)
{
	eng_nd_rwrite(room_ref, exception_ref, str, len);
	return 0;
}

XY_IMPL(int, nd_wwrite, unsigned, player_ref, void *, msg, size_t, len)
{
	uint32_t cur = corm_get_multi(fds_hd, &player_ref);
	const void *kp, *vp;
	int n = 0;

	if (cur != CM_MISS) {
		while (corm_next(&kp, &vp, cur)) {
			unsigned fd = *(const unsigned *)vp;

			if (axil_flags(fd) & DF_WEBSOCKET)
				n++;
		}
		corm_fin(cur);
	}
	eng_nd_wwrite(player_ref, msg, len);
	return n;
}

XY_IMPL(int, nd_twrites, unsigned, player_ref, char *, str, size_t, len)
{
	uint32_t cur = corm_get_multi(fds_hd, &player_ref);
	const void *kp, *vp;
	int n = 0;

	if (cur != CM_MISS) {
		while (corm_next(&kp, &vp, cur)) {
			unsigned fd = *(const unsigned *)vp;

			if (!(axil_flags(fd) & DF_WEBSOCKET))
				n++;
		}
		corm_fin(cur);
	}
	eng_nd_twrites(player_ref, str, len);
	return n;
}

XY_IMPL(int, nd_close, unsigned, player_ref)
{
	eng_nd_close(player_ref);
	return 0;
}

XY_DEF(int, on_demo, unsigned, player_ref, char *, message);

void
nd_demo_announce(unsigned player_ref)
{
	char msg[] = "hello from engine";
	on_demo(player_ref, msg);
}

void
nd_mods_load(void)
{
	/* Load game modules from the mods.load list (site mods/core pattern:
	 * one line per module, `#` comments, blank lines skipped), so modules
	 * land in the region subtree and their listeners dispatch to the game
	 * events the engine fires.
	 *
	 * A line is one of three things: a bare module name WITH a module in
	 * this tree -- loaded as `mods/<n>/<n>`, the in-tree layout -- a bare
	 * name WITHOUT one, which names an INSTALLED library by soname, or a
	 * path (anything containing `/`), taken verbatim. The first is what
	 * makes the one-repo-per-module layout (MODS.md §4) work: a sibling
	 * checkout is named by its own path, e.g.
	 *   ../axil-nd-level/level
	 * and needs no copying into the engine tree. The second is what makes
	 * "installable" true: xy_load() appends the suffix and hands the bare
	 * filename to dlopen(), which resolves it through the normal search
	 * path, so a module shipped as a package -- libnd-core.so plus its
	 * nd-core.so soname symlink -- just loads.
	 *
	 * Makefile's `mods:` target applies the identical test, so build and
	 * load never disagree about what a bare name means.
	 *
	 * Entries name the STEM, never the `*.so`: xy_load() appends the suffix
	 * itself (libxylem-module.c:114; libxylem-watch.c:54 notes it strips
	 * `.so` back off for the watch table), so writing `level.so` here
	 * makes it look for `level.so.so` and silently fail. Verified: the
	 * `mods/demo/demo.so` form booted with
	 *   mod_load_open_handle: _mod_load failed loading
	 *   'mods/demo/demo.so': ...so.so: cannot open shared object file
	 * and the suite died on `FAIL: on_demo frame missing`.
	 *
	 * The list file itself defaults to "mods.load" (cwd-relative), which is
	 * what every standalone run (test.sh, a bare `axil -m ./lib/axil-nd`)
	 * already expects. AXIL_ND_GLOBAL_MODS overrides it -- needed when the
	 * site embeds this engine as an XY module: the site's own axil process
	 * chdir()s into the SITE root (not this tree), so a cwd-relative
	 * "mods.load" there would resolve to the SITE's own list (mods/core/core.c
	 * reads the exact same relative name) and the two module tiers -- the
	 * site's own and the engine's global one -- would collide on one file.
	 * The site's start scripts set AXIL_ND_GLOBAL_MODS to this tree's own
	 * mods.load by an unambiguous path, keeping the two lists apart.
	 *
	 * The in-tree form's `mods/<n>/<n>` is resolved against the list
	 * file's OWN directory (dirname(list_path)), not cwd, for the same
	 * reason: dirname() of a bare "mods.load" is "." (unchanged standalone
	 * behavior), but dirname() of AXIL_ND_GLOBAL_MODS's absolute path is
	 * this tree's own root even though the site's axil process never
	 * chdir()s there. Without this, every in-tree entry -- "demo" among
	 * them -- would silently take the installed-soname branch instead,
	 * since `access("mods/demo/demo.c", R_OK)` is checked against the
	 * SITE's cwd and finds nothing there.
	 *
	 * A missing list fails loudly: silently substituting the demo module
	 * would hide a bad path (typo'd override, wrong cwd) behind a server
	 * that looks like it booted fine, with none of the intended modules
	 * actually present. */
	char base_dir[768];
	/* base_dir + "/mods/" + name + "/" + name + ".c", name up to 511 bytes
	 * (sizeof(line) - 1) twice over -- wider than `path` to absorb the
	 * ".c" suffix the in-tree probe adds. */
	char inpath[sizeof(base_dir) + 2 * 512 + 16];
	char path[sizeof(inpath) - 8];
	char line[512];
	char list_path_buf[sizeof(base_dir)];
	const char *list_path = getenv("AXIL_ND_GLOBAL_MODS");
	FILE *fp;
	if (!list_path || !*list_path)
		list_path = "mods.load";
	/* dirname() may modify its argument; snprintf()'s result into
	 * list_path_buf is that mutable copy. */
	snprintf(list_path_buf, sizeof(list_path_buf), "%s", list_path);
	snprintf(base_dir, sizeof(base_dir), "%s", dirname(list_path_buf));
	fp = fopen(list_path, "r");
	if (!fp) {
		fprintf(stderr,
			"nd_mods_load: cannot open global module list '%s': %s\n",
			list_path, strerror(errno));
		return;
	}
	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 &&
		       (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[len - 1] = '\0';
			len--;
		}
		if (len == 0 || line[0] == '#')
			continue;
		if (strchr(line, '/'))
			snprintf(path, sizeof(path), "%s", line);
		else {
			/* Same test Makefile's `mods:` target uses. A source file
			 * under mods/<n>/ is what makes a name in-tree; anything
			 * else is an installed soname. Both candidate paths are
			 * rooted at base_dir (the list file's own directory), not
			 * cwd -- see the comment above nd_mods_load(). */
			snprintf(inpath, sizeof(inpath), "%s/mods/%s/%s.c",
				base_dir, line, line);
			if (access(inpath, R_OK) == 0)
				snprintf(path, sizeof(path), "%s/mods/%s/%s",
					base_dir, line, line);
			else
				snprintf(path, sizeof(path), "%s", line);
		}
		if (xy_load(path) != XY_OK)
			fprintf(stderr, "nd_mods_load: module %s failed to load\n",
				path);
	}
	fclose(fp);
}

XY_IMPL(int, nd_flush, unsigned, player_ref)
{
	eng_nd_flush(player_ref);
	return 0;
}
