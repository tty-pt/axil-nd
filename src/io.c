/* io.c — J4.8 step 4: the engine's write path + fd registry, split out of
 * x/interface.c.  axil is the new ndc: every transport call is axil_*.
 *
 * Owns the biometric fd↔player pair (dplayer_hd / fds_hd) that was opened in
 * x/interface.c main() and is currently ALSO carried privately in
 * x/nd_xy.c — Phase 2 deletes the nd_xy.c copy and keeps these.
 *
 * Per-fd typewriter buffer (history×dedup) kept here; nd_io_flush_fd() is the
 * fd-level flush exposed to the on_axil_flush XY hook (libaxil-nd.c), the
 * "after any command" slot axil invokes.
 */

#include "uapi/io.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include <ttypt/axil.h>
#include <ttypt/corm.h>

#include "config.h"
#include "uapi/object.h"
#include "uapi/skel.h"

struct ioc {
	char buf[BUFSIZ];
	size_t len;
	unsigned n;
} ioc[FD_SETSIZE];

unsigned dplayer_hd, fds_hd;

void
nd_io_init(void)
{
	if (dplayer_hd)
		return;
	dplayer_hd = corm_open(NULL, "dplayer", CM_U32, CM_U32, 0xFF, 0);
	/* Single fd per player, with replace-on-attach semantics. This used to
	 * be CM_SORTED | CM_MULTIVALUE, but libcorm 0.8.0 only re-links the
	 * CM_MULTIVALUE duplicate chains (mv_next) in corm_rebuild_map, while
	 * corm_get_multi walks that chain directly without forcing a rebuild.
	 * Emptying the set (corm_del_all / deleting the last fd) then putting a
	 * fresh fd left stale links, and the next corm_next() returned success
	 * with a NULL `vp` -- eng_nd_wwrite() dereferenced it (SIGSEGV at
	 * io.c:207, via mcp_auth_success <- nd_player_login). A player always
	 * has exactly one live session, so the multivalue set bought nothing
	 * and cost a use-after-free class. A plain map makes attach/detach
	 * ordinary replace/delete, which corm keeps consistent. */
	fds_hd     = corm_open(NULL, "fds",     CM_U32, CM_U32, 0xFF, 0);
}

void
nd_io_attach(unsigned fd, unsigned player_ref)
{
	/* Replace, never accumulate: a relogin on a new socket must drop the
	 * previous fd, or every later write would also go to a descriptor that
	 * may already be gone. Clean up the reverse entry so eng_fd_player()
	 * never resolves the superseded socket. */
	nd_io_init();
	const void *old = corm_get(fds_hd, &player_ref);

	if (old) {
		unsigned ofd = *(const unsigned *)old;

		if (ofd != fd)
			corm_del(dplayer_hd, &ofd);
	}
	corm_put(dplayer_hd, &fd, &player_ref);
	corm_put(fds_hd, &player_ref, &fd);
}

void
nd_io_detach(unsigned fd)
{
	const void *v = corm_get(dplayer_hd, &fd);

	if (!v)
		return;

	unsigned player_ref = *(const unsigned *)v;

	corm_del(dplayer_hd, &fd);
	/* Only clear the forward entry if it still points at this socket. The
	 * fd may already have been re-attached to a newer connection (raw
	 * guest relogging while the old socket was being closed), and blindly
	 * clearing it would orphan the live session. */
	const void *cur = corm_get(fds_hd, &player_ref);

	if (cur && *(const unsigned *)cur == fd)
		corm_del(fds_hd, &player_ref);
}

/* The single socket currently bound to a player, or -1. */
static int
nd_player_fd(unsigned player_ref)
{
	const void *v = corm_get(fds_hd, &player_ref);

	if (!v)
		return -1;
	return (int)*(const unsigned *)v;
}

void
nd_io_reset(unsigned fd)
{
	ioc[fd].n = 0;
}

unsigned
eng_fd_player(unsigned fd)
{
	const void *v = corm_get(dplayer_hd, &fd);

	if (v)
		return *(const unsigned *)v;
	return 0;
}

static void
ioc_flush(int fd)
{
	char buf[BUFSIZ + 32], *b = buf;
	/* Lone-LF expansion at most doubles the body. */
	char out[2 * (BUFSIZ + 32)];

	if (!ioc[fd].len)
		return;

	if (ioc[fd].n > 1)
		b += snprintf(buf, sizeof(buf), "(%ux) ", ioc[fd].n);

	b += snprintf(b, sizeof(buf) - (b - buf), "%s", ioc[fd].buf);

	if (axil_flags(fd) & DF_WEBSOCKET) {
		/* Game text bypasses the PTY, so no ONLCR translation applies:
		 * expand lone LFs to CRLF for the terminal. Existing CRLF pairs
		 * pass through untouched; BCP binary never reaches this path. */
		size_t n = 0, cap = sizeof(out) - 1;

		for (char *p = buf; p < b && n < cap; p++) {
			if (*p == '\n' && (p == buf || p[-1] != '\r')) {
				if (n + 1 >= cap)
					break;
				out[n++] = '\r';
			}
			out[n++] = *p;
		}
		axil_write(fd, out, n);
	} else
		axil_write(fd, buf, b - buf);
	ioc[fd].len = 0;
	memset(ioc[fd].buf, 0, sizeof(ioc[fd].buf));
}

void
nd_io_flush_fd(unsigned fd)
{
	ioc_flush(fd);
}

void
eng_nd_flush(unsigned player_ref)
{
	int fd = nd_player_fd(player_ref);

	if (fd >= 0)
		ioc_flush(fd);
}

void
eng_nd_write(unsigned player_ref, char *str, size_t len)
{
	int fd = nd_player_fd(player_ref);

	if (fd < 0)
		return;
	if (memcmp(str, ioc[fd].buf, len)) {
		ioc_flush(fd);
		memcpy(ioc[fd].buf, str, len);
		ioc[fd].n = 1;
		ioc[fd].len = len;
	} else
		ioc[fd].n++;
}

void
nd_dwritef(unsigned player_ref, const char *fmt, va_list args)
{
	static char buf[BUFSIZ];
	ssize_t len = vsnprintf(buf, sizeof(buf), fmt, args);

	eng_nd_write(player_ref, buf, len);
}

void
eng_nd_rwrite(unsigned room_ref, unsigned exception_ref, char *str, size_t len)
{
	uint32_t cur = corm_iter(contents_hd, &room_ref, CM_RANGE);
	const void *kp, *vp;

	while (corm_next(&kp, &vp, cur)) {
		unsigned tmp_ref = *(const unsigned *)kp;
		const OBJ *tmp;

		if (tmp_ref == exception_ref)
			continue;

		tmp = corm_get(obj_hd, &tmp_ref);
		if (tmp && tmp->type == TYPE_ENTITY)
			eng_nd_write(tmp_ref, str, len);
	}
	corm_fin(cur);
}

void
nd_dowritef(unsigned player_ref, const char *fmt, va_list args)
{
	char buf[BUFFER_LEN];
	size_t len;
	const OBJ *player;

	len = vsnprintf(buf, sizeof(buf), fmt, args);
	player = corm_get(obj_hd, &player_ref);
	if (!player)
		return;
	eng_nd_rwrite(player->location, player_ref, buf, len);
}

void nd_tdwritef(unsigned player_ref, const char *fmt, va_list args) {
	static char buf[BUFSIZ];
	ssize_t len = vsnprintf(buf, sizeof(buf), fmt, args);
	int fd = nd_player_fd(player_ref);

	if (fd < 0)
		return;
	if (!(axil_flags(fd) & DF_WEBSOCKET))
		axil_write(fd, buf, len);
}

void eng_nd_wwrite(unsigned player_ref, void *msg, size_t len) {
	int fd = nd_player_fd(player_ref);

	if (fd < 0)
		return;
	if ((axil_flags(fd) & DF_WEBSOCKET))
		axil_write(fd, msg, len);
}

/* explicit-length non-WebSocket (raw telnet) write: the XY nd_twrites
 * provider target (nd_tdwritef is the variadic engine-side sibling). */
void
eng_nd_twrites(unsigned player_ref, char *str, size_t len)
{
	int fd = nd_player_fd(player_ref);

	if (fd < 0)
		return;
	if (!(axil_flags(fd) & DF_WEBSOCKET))
		axil_write(fd, str, len);
}

void
eng_nd_close(unsigned player_ref)
{
	int fd = nd_player_fd(player_ref);

	if (fd >= 0)
		axil_close(fd);
}

char *plural(char *singular) {
	static char plural[BUFSIZ], *last, *prev;
	char *space = strchr(singular, ' ');
	size_t len = space ? (size_t)(space - singular) : strlen(singular);
	memset(plural, 0, sizeof(plural));
	strncpy(plural, singular, len);
	last = plural + len - 1;
	prev = last - 1;

	switch (*last) {
		case 'y':
			*last = 'i';
			/* fall through */
		case 's':
		case 'x':
		case 'z':
			last = plural + strlcat(plural, "es", sizeof(plural));
			break;
		case 'h':
			if (*prev != 'g') {
				last = plural + strlcat(plural, "es", sizeof(plural));
				break;
			}
			/* fall through */
		default:
			last = plural + strlcat(plural, "s", sizeof(plural));
	}

	strlcat(last, singular + len, sizeof(plural) - len);
	return plural;
}