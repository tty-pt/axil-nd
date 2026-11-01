#ifndef ND_LAW_H
#define ND_LAW_H

/*
 * law.h — the kernel's side of the NeverDark API (libxylem XY): the universal
 * treaty every module gets. Transport (io), storage, and command
 * registration. Nothing here knows the object model, rooms, terrain, view,
 * the client protocol or speech — those are the reference universe, in
 * nd/world.h (convention). Region ownership (governance) is in nd/regions.h.
 *
 * Layering:
 *     nd/law.h      this file — law: io, storage, command
 *     nd/regions.h  law: public governance (region ownership)
 *     nd/world.h    convention: the reference universe (object/terrain/…)
 *     nd/xy.h       umbrella over all three (what game modules include)
 *
 * Include the narrowest header you need: a kernel-side object should include
 * <nd/law.h>; a game module may include <nd/xy.h>.
 *
 * Replaces the engine's dlopen mod-load / `struct nd` handoff / SIC macros
 * (interface.c mod_load + `*ind = nd`, uapi/type.h SIC_DECL/SIC_DEF/SIC_CALL)
 * with the XY host/module protocol:
 *
 *   - The ENGINE (host) provides every hook below via XY_IMPL in ONE TU that
 *     must NOT include this header (site mods/common/common.h rule). The hook
 *     name with no module override dispatches to the engine's implementation
 *     within the caller's region subtree.
 *   - GAME MODULES include this header freely, then call the hooks as plain C
 *     (XY_DECL forwards through xy_call to the engine provider). Modules
 *     implement their own game events (see nd/hooks.h) with XY_IMPL.
 *   - `get_xy_ptr()` in xy-mod.h supplies the injected xy context
 *     (module_path, region_id, load/unload, ...) — the replacement for
 *     `struct nd *ind = dlsym(sl, "nd"); *ind = nd;`.
 *
 * HOOK SHAPE CONSTRAINT: XY_DECL/XY_DEF emit `ftype ret;` in the inline
 * dispatcher and pack args into a struct literal, so:
 *   - every hook MUST return a scalar type (use `int`, 0 on success / >0 for
 *     fds written, negative XY_ERR_* on failure; `unsigned`/`char *` and
 *     small structs work too) — `void` does not compile;
 *   - every argument MUST be a plain value — `va_list` does not survive the
 *     arg-struct copy. The engine's variadic formatting wrappers
 *     (nd_writef/nd_owritef/nd_twritef, dnotify_wts*) stay engine-side;
 *     modules snprintf into a buffer and call nd_write/nd_rwrite with an
 *     explicit length.
 *   - struct return types must be <= XY_MAX_RET_SIZE (4096 B).
 *   - hooks need >=1 (type, name) arg pair (zero-arg does not compile;
 *     me_get takes a dummy); no array-typed args (pos_t decays — positions
 *     are passed as coord_t *).
 *
 * This header is deliberately SELF-CONTAINED (raw types from
 * nd/xy-types.h). It does NOT include the uapi headers: those declare
 * the matching fn-pointer globals (e.g. `nd_write_t nd_write;`) which would
 * collide with the XY_DECL inline of the same name in a module TU.
 *
 * Region model (st_* ownership): a game module claims a subspace with
 * `xy_require_claim()` in its xy_install(); `xy_with_region()`/regions scope
 * per-owner plugin dispatch. See nd/regions.h and ST.md.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
/* nd_printf below calls vsnprintf. A module TU that includes this header
 * first -- which is the normal order -- must not depend on the module having
 * pulled in <stdio.h> itself: the in-tree demo happened to include stdio
 * before this header and so never showed the gap, but a first out-of-tree
 * module that did not died with "implicit declaration of vsnprintf". A
 * self-contained public header has to carry its own dependencies. */
#include <stdio.h>

#include <ttypt/xy.h>

#include "law-types.h"

/* ------------------------------------------------------------------ storage */

/* Corm table handle lookup and registration */
XY_DECL(unsigned, nd_table_handle, unsigned, hd_idx);
XY_DECL(int, nd_table_handle_set, unsigned, hd_idx, unsigned, val);

/* ------------------------------------------------------------------ io */

/* fd → player, used to route I/O for a connected socket; NOTHING on miss */
XY_DECL(unsigned, fd_player, unsigned, fd);

/* one player, snprintf'd text with explicit length; #fds written */
XY_DECL(int, nd_write, unsigned, player_ref, char *, str, size_t, len);

/* socket-targeted writes: nd_wwrite → DF_WEBSOCKET fds only,
 * nd_twrites → non-WebSocket (raw telnet) fds only. */
XY_DECL(int, nd_wwrite, unsigned, player_ref, void *, msg, size_t, len);
XY_DECL(int, nd_twrites, unsigned, player_ref, char *, str, size_t, len);

/* close / flush all fds of a player */
XY_DECL(int, nd_close, unsigned, player_ref);
XY_DECL(int, nd_flush, unsigned, player_ref);

/* snprintf'd text to one player (MODS.md §0.3).
 *
 * Every write hook here takes an explicit length because a `va_list` cannot
 * cross the XY boundary (see HOOK SHAPE CONSTRAINT above), so the old modules'
 * nd_writef/nd_owritef/nd_twritef have no direct equivalent. This is the
 * ergonomic stand-in: a module formats and writes in one call.
 *
 * A static inline rather than a hook on purpose -- it adds nothing the module
 * cannot already do with snprintf + nd_write, and it keeps the formatting
 * entirely on the module side of the bus. Returns the byte count, or a
 * negative value if the text was truncated; truncation is a bug in the
 * caller's format, not a runtime condition, so it is not silent. */
static inline int
nd_printf(unsigned player_ref, char *fmt, ...)
{
	char buf[1024];
	va_list va;
	int n;

	va_start(va, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n < 0)
		return n;
	nd_write(player_ref, buf, (size_t) n < sizeof(buf) ? (size_t) n
	                                                    : sizeof(buf) - 1);
	return n;
}

static inline int
nd_tprintf(unsigned player_ref, char *fmt, ...)
{
	char buf[1024];
	va_list va;
	int n;

	va_start(va, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n < 0)
		return n;
	return nd_twrites(player_ref, buf,
			  (size_t) n < sizeof(buf) ? (size_t) n : sizeof(buf) - 1);
}

static inline void eng_nd_flush(unsigned player_ref) { nd_flush(player_ref); }
static inline void eng_nd_close(unsigned player_ref) { nd_close(player_ref); }
static inline void eng_nd_write(unsigned player_ref, char *str, size_t len) { nd_write(player_ref, str, len); }
static inline void eng_nd_wwrite(unsigned player_ref, void *msg, size_t len) { nd_wwrite(player_ref, msg, len); }

static inline void
nd_writef(unsigned player_ref, const char *fmt, ...)
{
	char buf[1024];
	va_list va;
	int n;
	va_start(va, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n > 0)
		nd_write(player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

/* There is deliberately no nd_owritef: it was a copy of nd_writef and so
 * echoed "<me> goes north." to the actor. To tell a room's OTHER occupants,
 * format and call nd_rwrite(room_ref, actor_ref, str, len). */

static inline void
nd_twritef(unsigned player_ref, const char *fmt, ...)
{
	char buf[1024];
	va_list va;
	int n;
	va_start(va, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n > 0)
		nd_twrites(player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

void nd_io_init(void);
void nd_io_attach(unsigned fd, unsigned player_ref);
void nd_io_detach(unsigned fd);
void nd_io_reset(unsigned fd);
void nd_io_flush_fd(unsigned fd);

/* nd_last(ret) — the sic_last() port (MODS.md §0.3).
 *
 * sic_last() handed a module the return value of the module that ran before
 * it in the current dispatch, which is how a tenant module chains its listener
 * families (reading predecessor return values and folding adjustments in). libxylem has the same
 * previous one decided and folds its own adjustment in. libxylem has the same
 * primitive, xy_last() (xy.h:349); this is just the name the ported modules
 * read.
 *
 * It goes through the INJECTED context (`xy.last`) rather than the global
 * `xy_last`, because a module links no libxylem at all: <ttypt/xy-mod.h>
 * declares `static struct xy_ctx xy` and the host fills it in via
 * get_xy_ptr(). So <ttypt/xy-mod.h> has to be included BEFORE this header --
 * which is exactly MODS.md §6's porting rule, and if it is missed `xy` is
 * undeclared at the use site, which is the clearest available signal. */
#define nd_last(ret) (xy.last(ret))

/* -------------------------------------------------------------- storage */

/* map-cursor API (hds are engine-side ids): put/get by map id */
XY_DECL(unsigned, nd_put, unsigned, hd, void *, key, void *, data);
XY_DECL(unsigned, nd_get, unsigned, hd, void *, value, void *, key);
XY_DECL(int, nd_open, char *, type, char *, iden, char *, anon, unsigned, flags);
XY_DECL(unsigned, nd_iter, unsigned, hd, void *, key);
XY_DECL(int, nd_next, void *, key, void *, data, unsigned, cur);
XY_DECL(int, nd_fin, unsigned, cur);
XY_DECL(int, nd_len_reg, char *, iden, size_t, len);
/* corm secondary index: keeps `link` (a primary table) indexed by the keys
 * `assoc` derives from each row. Both handles are module-facing (nd_open
 * tags or enum hd); the engine resolves them. Hook-ified from the engine's
 * raw nd_assoc (uapi/io.h:101).
 *
 * Returns int, not the engine's void: XY_IMPL cannot express a void return
 * (it does `sizeof(ftype)` and `ftype result = ...`, both ill-formed for
 * void), and callers ignore the result. The engine body eng_nd_assoc keeps
 * the void signature. */
XY_DECL(int, nd_assoc, unsigned, hd, unsigned, link, nd_assoc_cb_p, assoc);

/* command registration (command layer) */
typedef void nd_cb_t(int fd, int argc, char *argv[]);
XY_DECL(int, nd_register, char *, str, nd_cb_t *, cb, unsigned, flags);

/* asset directory resolver */
XY_DECL(int, nd_asset_dir, unsigned, kind, char *, out, size_t, cap);

/*
 * Declarative game data. A tenant module owns a pipe-delimited table
 * (`<table>.dsv`, one row per line, `#` comments, blank lines ignored) and
 * asks the engine to feed it row by row, instead of compiling the rows into C.
 * The engine knows nothing about what a row means. Lookup order:
 *   $AXIL_ND_DATA, dirname($AXIL_ND_DB), external/<module>/data,
 *   ../<module>/data, data/<module>, <prefix>/share/axil-nd/<module>.
 * `row` receives the split fields (in-place, valid for the call only) and
 * returns 0 to continue or <0 to abort. This is a plain exported function,
 * not an XY hook: xy_install() of a tenant module cannot dispatch hooks yet. Returns rows consumed, or -1 when
 * no table file was found.
 */
int nd_table_load(const char *module, const char *table,
		nd_table_row_fn row, void *user);

#endif /* ND_LAW_H */
