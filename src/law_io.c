#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#include <ttypt/corm.h>
#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

static struct {
	char buf[BUFSIZ];
	size_t len;
	unsigned n;
} ioc[FD_SETSIZE];

#define ND_IO_PFD_CAP  2048u
#define ND_IO_PFD_MASK (ND_IO_PFD_CAP - 1u)

typedef struct {
	unsigned player_ref;
	unsigned fd;
	uint8_t  used;
} nd_io_pfd_slot_t;

static nd_io_pfd_slot_t pfd_map[ND_IO_PFD_CAP];
static unsigned fd_to_player[FD_SETSIZE];
static uint8_t  fd_attached[FD_SETSIZE];

static unsigned dplayer_hd, fds_hd;

void nd_store_set_hd(unsigned idx, unsigned val);

static inline uint32_t
pfd_hash(unsigned key)
{
	return (key * 2654435761u) & ND_IO_PFD_MASK;
}

static int
pfd_map_get(unsigned player_ref)
{
	uint32_t i = pfd_hash(player_ref);
	for (uint32_t probe = 0; probe < ND_IO_PFD_CAP; probe++) {
		uint32_t idx = (i + probe) & ND_IO_PFD_MASK;
		if (!pfd_map[idx].used)
			return -1;
		if (pfd_map[idx].player_ref == player_ref)
			return (int)pfd_map[idx].fd;
	}
	return -1;
}

static void
pfd_map_put(unsigned player_ref, unsigned fd)
{
	uint32_t i = pfd_hash(player_ref);
	for (uint32_t probe = 0; probe < ND_IO_PFD_CAP; probe++) {
		uint32_t idx = (i + probe) & ND_IO_PFD_MASK;
		if (!pfd_map[idx].used || pfd_map[idx].player_ref == player_ref) {
			pfd_map[idx].player_ref = player_ref;
			pfd_map[idx].fd = fd;
			pfd_map[idx].used = 1;
			return;
		}
	}
}

static void
pfd_map_del(unsigned player_ref)
{
	uint32_t i = pfd_hash(player_ref);
	for (uint32_t probe = 0; probe < ND_IO_PFD_CAP; probe++) {
		uint32_t idx = (i + probe) & ND_IO_PFD_MASK;
		if (!pfd_map[idx].used)
			return;
		if (pfd_map[idx].player_ref == player_ref) {
			/* Backward-shift deletion: zero tombstones ever accumulate */
			uint32_t hole = idx;
			uint32_t cur = (hole + 1u) & ND_IO_PFD_MASK;
			while (pfd_map[cur].used) {
				uint32_t home = pfd_hash(pfd_map[cur].player_ref);
				if (((cur - home) & ND_IO_PFD_MASK) >= ((cur - hole) & ND_IO_PFD_MASK)) {
					pfd_map[hole] = pfd_map[cur];
					hole = cur;
				}
				cur = (cur + 1u) & ND_IO_PFD_MASK;
			}
			pfd_map[hole].used = 0;
			return;
		}
	}
}

void
nd_io_init(void)
{
	if (dplayer_hd)
		return;
	dplayer_hd = corm_open(NULL, "dplayer", CM_U32, CM_U32, 0xFF, 0);
	fds_hd     = corm_open(NULL, "fds",     CM_U32, CM_U32, 0xFF, 0);
	nd_store_set_hd(ND_HD_FD, fds_hd);
}

void
nd_io_attach(unsigned fd, unsigned player_ref)
{
	nd_io_init();
	int old_fd = pfd_map_get(player_ref);
	if (old_fd >= 0 && (unsigned)old_fd != fd) {
		unsigned ofd = (unsigned)old_fd;
		if (ofd < FD_SETSIZE) {
			fd_attached[ofd] = 0;
			fd_to_player[ofd] = 0;
		}
		corm_del(dplayer_hd, &ofd);
	}
	if (fd < FD_SETSIZE && fd_attached[fd] && fd_to_player[fd] != player_ref) {
		unsigned old_p = fd_to_player[fd];
		if (pfd_map_get(old_p) == (int)fd) {
			pfd_map_del(old_p);
			corm_del(fds_hd, &old_p);
		}
	}

	if (fd < FD_SETSIZE) {
		fd_to_player[fd] = player_ref;
		fd_attached[fd] = 1;
	}
	pfd_map_put(player_ref, fd);
	corm_put(dplayer_hd, &fd, &player_ref);
	corm_put(fds_hd, &player_ref, &fd);
}

void
nd_io_detach(unsigned fd)
{
	unsigned player_ref = 0;
	int found = 0;

	if (fd < FD_SETSIZE) {
		if (fd_attached[fd]) {
			player_ref = fd_to_player[fd];
			fd_attached[fd] = 0;
			fd_to_player[fd] = 0;
			found = 1;
		}
	} else if (dplayer_hd) {
		const void *v = corm_get(dplayer_hd, &fd);
		if (v) {
			player_ref = *(const unsigned *)v;
			found = 1;
		}
	}
	if (!found)
		return;

	corm_del(dplayer_hd, &fd);
	if (pfd_map_get(player_ref) == (int)fd) {
		pfd_map_del(player_ref);
		corm_del(fds_hd, &player_ref);
	}
}

static inline int
nd_player_fd(unsigned player_ref)
{
	return pfd_map_get(player_ref);
}

void
nd_io_reset(unsigned fd)
{
	if (fd < FD_SETSIZE)
		ioc[fd].n = 0;
}

static void
ioc_flush(int fd)
{
	char buf[BUFSIZ + 32], *b = buf;
	char out[2 * (BUFSIZ + 32)];

	if (fd < 0 || fd >= FD_SETSIZE || !ioc[fd].len)
		return;

	if (ioc[fd].n > 1)
		b += snprintf(buf, sizeof(buf), "(%ux) ", ioc[fd].n);

	b += snprintf(b, sizeof(buf) - (b - buf), "%s", ioc[fd].buf);

	if (axil_flags(fd) & DF_WEBSOCKET) {
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

XY_IMPL(unsigned, fd_player, unsigned, fd)
{
	if (fd < FD_SETSIZE)
		return fd_attached[fd] ? fd_to_player[fd] : 0;
	if (!dplayer_hd)
		return 0;
	const void *v = corm_get(dplayer_hd, &fd);
	return v ? *(const unsigned *)v : 0;
}

unsigned
eng_fd_player(unsigned fd)
{
	return fd_player(fd);
}

XY_IMPL(int, nd_write, unsigned, player_ref, char *, str, size_t, len)
{
	int fd = nd_player_fd(player_ref);
	if (fd < 0 || fd >= FD_SETSIZE)
		return 0;

	if (memcmp(str, ioc[fd].buf, len)) {
		ioc_flush(fd);
		memcpy(ioc[fd].buf, str, len);
		ioc[fd].n = 1;
		ioc[fd].len = len;
	} else
		ioc[fd].n++;

	return 1;
}

void
nd_dwritef(unsigned player_ref, const char *fmt, va_list args)
{
	char buf[1024];
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	if (n <= 0)
		return;
	nd_write(player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

XY_IMPL(int, nd_wwrite, unsigned, player_ref, void *, msg, size_t, len)
{
	int fd = nd_player_fd(player_ref);
	if (fd < 0)
		return 0;
	if (axil_flags(fd) & DF_WEBSOCKET) {
		axil_write(fd, msg, len);
		return 1;
	}
	return 0;
}

XY_IMPL(int, nd_twrites, unsigned, player_ref, char *, str, size_t, len)
{
	int fd = nd_player_fd(player_ref);
	if (fd < 0)
		return 0;
	if (!(axil_flags(fd) & DF_WEBSOCKET)) {
		axil_write(fd, str, len);
		return 1;
	}
	return 0;
}

XY_IMPL(int, nd_player_is_ws, unsigned, player_ref)
{
	int fd = nd_player_fd(player_ref);
	return (fd >= 0 && (axil_flags(fd) & DF_WEBSOCKET)) ? 1 : 0;
}

void
nd_tdwritef(unsigned player_ref, const char *fmt, va_list args)
{
	char buf[1024];
	int n = vsnprintf(buf, sizeof(buf), fmt, args);
	if (n <= 0)
		return;
	nd_twrites(player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

XY_IMPL(int, nd_close, unsigned, player_ref)
{
	int fd = nd_player_fd(player_ref);
	if (fd >= 0)
		axil_close(fd);
	return 0;
}

XY_IMPL(int, nd_flush, unsigned, player_ref)
{
	int fd = nd_player_fd(player_ref);
	if (fd >= 0)
		ioc_flush(fd);
	return 0;
}

XY_IMPL(int, on_axil_flush, socket_t, fd)
{
	ioc_flush(fd);
	return 0;
}

int nd_is_unmodded(void);
void nd_bare_motd(socket_t fd);

static int
is_http_method(const char *s)
{
	static const char *const methods[] = {
		"GET", "POST", "HEAD", "PUT", "OPTIONS", "DELETE", "PATCH", "PRI"
	};
	while (*s == ' ')
		s++;
	for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
		size_t n = strlen(methods[i]);
		if (strncmp(s, methods[i], n) == 0 && s[n] == ' ')
			return 1;
	}
	return 0;
}

XY_IMPL(int, on_axil_parse, socket_t, fd, unsigned char *, input, int, nread)
{
	if (is_http_method((const char *)input))
		return nread;
	if (nd_is_unmodded()) {
		nd_bare_motd(fd);
		return -1;
	}
	return nread;
}

XY_IMPL(int, on_axil_exit, void)
{
	corm_save();
	return 0;
}
