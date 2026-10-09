#ifndef UAPI_IO_H
#define UAPI_IO_H

#include <stdarg.h>
#include <stddef.h>
#include "azoth.h"

/* `enum hd` moved to nd/hd.h so game modules can name the same tables
 * (MODS.md §0.2). It is included rather than pasted so the two copies
 * cannot drift. */
#include "../nd/hd.h"

/* The engine-side table each `enum hd` resolves to, filled by shared_init()
 * (world.c). Replaces indexing nd.hds[] from the providers, and is the only
 * place the enum -> corm mapping exists. */
extern unsigned nd_hds[HD_MAX];

/* Resolve a module-facing handle to the corm table it names: an `enum hd`
 * indexes nd_hds[], a module-opened tag indexes the module-table registry.
 * Returns 0 for a handle in neither namespace (an unopened slot, or a bug in
 * the module), which corm_* reports as a miss rather than corrupting a table. */
unsigned hd_resolve(unsigned hd);

/* nd_open()'s engine half: corm_open + registry insert, returning a tagged
 * nd_hd_mod() handle (or 0 if the table could not be opened). */
unsigned hd_mod_open(char *type, char *iden, char *anon, unsigned flags);

typedef unsigned fd_player_t(unsigned fd);
fd_player_t eng_fd_player;

/* typedef int fds_has_t(unsigned player); */
/* fds_has_t fds_has; */

typedef void nd_close_t(unsigned player);
nd_close_t eng_nd_close, eng_nd_flush;

typedef void nd_write_t(unsigned player_ref, char *str, size_t len);
nd_write_t eng_nd_write;

typedef void nd_dwritef_t(unsigned player_ref, const char *fmt, va_list args);
nd_dwritef_t nd_dwritef;

static inline void
nd_writef(unsigned player_ref, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	nd_dwritef(player_ref, fmt, va);
	va_end(va);
}

typedef void nd_rwrite_t(unsigned room_ref, unsigned exception_ref, char *str, size_t len);
nd_rwrite_t eng_nd_rwrite;

typedef void nd_dowritef_t(unsigned player_ref, const char *format, va_list args);
nd_dowritef_t nd_dowritef;

static inline void
nd_owritef(unsigned player_ref, char *format, ...)
{
	va_list args;
	va_start(args, format);
	nd_dowritef(player_ref, format, args);
	va_end(args);
}

typedef void nd_tdwritef_t(unsigned player_ref, const char *fmt, va_list args);
nd_tdwritef_t nd_tdwritef;

static inline void nd_twritef(unsigned player_ref, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	nd_tdwritef(player_ref, fmt, va);
	va_end(va);
}

typedef void nd_wwrite_t(unsigned player_ref, void *msg, size_t len);
nd_wwrite_t eng_nd_wwrite;

/* non-variadic non-WebSocket write (XY nd_twrites provider target). */
void eng_nd_twrites(unsigned player_ref, char *str, size_t len);

extern unsigned fds_hd;
extern unsigned dplayer_hd;

void nd_io_init(void);
void nd_io_attach(unsigned fd, unsigned player_ref);
void nd_io_detach(unsigned fd);
void nd_io_reset(unsigned fd);
void nd_io_flush_fd(unsigned fd);

typedef unsigned (nd_put_t)(unsigned, void *, void *);
nd_put_t nd_put, nd_get;
nd_put_t shared_put, shared_get;

typedef int (nd_open_t)(char *, char *, char *, unsigned);
nd_open_t nd_open;

typedef int nd_assoc_cb_t(const void ** const skey,
		const void * const key,
		const void * const data);

typedef void nd_assoc_t(unsigned hd, unsigned link, nd_assoc_cb_t assoc);
nd_assoc_t nd_assoc, shared_assoc;

typedef void nd_len_reg_t(char *iden, size_t len);
nd_len_reg_t nd_len_reg;

typedef unsigned (nd_iter_t)(unsigned, void *);
nd_iter_t nd_iter;

typedef int (nd_next_t)(void *, void *, unsigned cur);
nd_next_t nd_next;

typedef void (nd_fin_t)(unsigned cur);
nd_fin_t nd_fin;

typedef void nd_cb_t(int fd, int argc, char *argv[]);
typedef void nd_register_t(char *, nd_cb_t *, unsigned);
nd_register_t eng_nd_register;

typedef void mod_load_t(char *fname);
mod_load_t mod_load;

typedef char *plural_t(char *singular);
plural_t plural;

static inline char *plural_maybe(char *singular, int number) {
	return number == 1 || number == -1 ? singular : plural(singular);
}

#endif
