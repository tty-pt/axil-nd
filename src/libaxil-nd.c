#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

/* xy-mod.h must come first so it defines the module xy context used by the
 * XY_CALL dispatches in this translation unit */
#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/corm.h>

XY_DECL(int, axil_tty_active, socket_t, fd);
XY_DECL(int, axil_tty_attach, socket_t, fd);
XY_DECL(int, axil_tty_input, socket_t, fd, unsigned char *, input, int, nread);
XY_DECL(int, axil_tty_owns, socket_t, fd);

/* axil-tty's own route handler, registered by xy_install() below. Called
 * directly rather than through the xy bus: xy_load() is a load-time side
 * effect, and a route should not depend on one. */
int axil_tty_handle_tty(socket_t cfd, char *body);

#include <arpa/telnet.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same sentinel as uapi/object.h / nd/xy-types.h -- not included directly:
 * this file folds nd_xy.c/nd_events.c/nd_api.c in as one TU (see the bottom
 * of this file), and uapi/object.h's eng_object_* typedefs conflict with
 * those files' own XY_IMPL-based definitions of the same names. */
#ifndef NOTHING
#define NOTHING ((unsigned) -1)
#endif

#ifndef AXIL_PREFIX
#define AXIL_PREFIX "/usr/local"
#endif

#ifndef AXIL_HTDOCS
#define AXIL_HTDOCS AXIL_PREFIX "/share/axil-nd/htdocs"
#endif

/* axil-tty's route, which this module serves on axil-tty's behalf rather than
 * playing a game on it. Must match AXIL_TTY_ROUTE in libaxil-tty.c. */
#define ND_TTY_ROUTE "/tty"

/* ------------------------------------------------------------------ */
/* axil hook implementations (real engine, wired to world.c)           */
/* ------------------------------------------------------------------ */

void nd_io_init(void);
void nd_io_attach(unsigned fd, unsigned player_ref);
void nd_io_detach(unsigned fd);
void nd_io_reset(unsigned fd);
void nd_mods_load(void);
void st_init(void);
void nd_demo_announce(unsigned player_ref);
void nd_event_announce(unsigned player_ref, unsigned loc_ref);
/* same-TU XY providers from nd_xy.c (declared for the axil hooks above). */
unsigned fd_player(unsigned fd);
int nd_write(unsigned player_ref, char *str, size_t len);
int nd_flush(unsigned player_ref);
int nd_world_init(int argc, char **argv);
/* world.c exports (Phase 4: real auth, lifecycle, command table). */
unsigned nd_connect(int fd);
void nd_register_commands(void);
void nd_command(int fd, int argc, char *argv[]);
void nd_vim(int fd, int argc, char *argv[]);
void nd_update(unsigned long long dt);
void nd_disconnect(int fd);
void close_all(int i);
void nd_set_auth_ready(int ready);
void nd_password_line(int fd, const char *name, const char *password);

/* world.c's command table, walked directly by the WebSocket frame dispatch
 * below: axil's own cmd_proc() is static, so a watched WS fd has no axil-side
 * route from a frame payload to a registered handler. */
extern struct cmd_slot cmds[];

/* HTTP request-line detection for the raw branch: a non-WebSocket fd is
 * still mid-upgrade until the first WS frame, so GET/POST/etc. must be
 * passed through untouched (they drive axil's request_handle -> 101), not
 * treated as raw commands. */
static int
is_http_method(const char *s)
{
  static const char *const methods[] = {
    "GET", "POST", "HEAD", "PUT", "OPTIONS", "DELETE", "PATCH", "PRI"
  };
  size_t i;

  while (*s == ' ')
    s++;
  for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
    size_t n = strlen(methods[i]);

    if (strncmp(s, methods[i], n) == 0 && s[n] == ' ')
      return 1;
  }
  return 0;
}

/* Strip telnet IAC negotiation from a raw stream in place; returns the
 * cleaned length.  Request bytes ride cmd_parse/do_GET afterwards. */
static int
strip_telnet(unsigned char *input, int nread)
{
  int in = 0, out = 0;

  while (in < nread) {
    if (input[in] == IAC) {
      if (in + 1 >= nread)
        break; /* dangling IAC: drop it */
      switch (input[in + 1]) {
      case WILL:
      case WONT:
      case DO:
      case DONT:
        if (in + 2 >= nread)
          return out; /* incomplete 3-byte; stop consuming */
        in += 3;
        break;
      case SB:
        in += 2;
        while (in + 1 < nread &&
               !(input[in] == IAC && input[in + 1] == SE))
          in++;
        if (in + 1 < nread)
          in += 2; /* consume trailing IAC SE */
        break;
      default:
        in += 2; /* IAC NOP / DM / other 2-byte */
      }
    } else {
      input[out++] = input[in++];
    }
  }
  return out;
}

/* True when fd is axil-tty's own route, so the game must keep off it.
 *
 * Both checks are needed, because axil dispatches on_axil_connect to every
 * loaded module in load order and this one runs FIRST -- before axil-tty's
 * hook sets owns_client. So axil_tty_owns() is still 0 in the very window
 * where the wrong answer does the damage, and only DOCUMENT_URI can answer
 * it. After axil-tty's hook runs, owns_client is the authoritative answer and
 * DOCUMENT_URI keeps agreeing, so the two together are stable across the whole
 * connection.
 *
 * DOCUMENT_URI lives in the per-descriptor env for the connection's lifetime
 * (axil clears it on close), so it is available in every hook below, not just
 * at upgrade time. */
static int
nd_tty_owned(socket_t fd)
{
  if (axil_tty_owns(fd))
    return 1;
  char doc_uri[BUFSIZ] = {0};
  if (axil_env_get(fd, doc_uri, sizeof(doc_uri), "DOCUMENT_URI") == 0 &&
      strcmp(doc_uri, ND_TTY_ROUTE) == 0)
    return 1;
  return 0;
}

XY_IMPL(int, on_axil_connect, socket_t, fd)
{
  /* Not our socket. /tty is axil-tty's route, and it opens the PTY and
   * auto-spawns the shell on first NAWS in its own on_axil_connect -- which
   * runs right after this one. Without this gate a /tty client got a full game
   * login first (nd_connect created a player and wrote frames) and then the
   * shell, with both attached to the same socket. Every hook below has the
   * same gate: a single place that owns the connection, no game on it. */
  if (nd_tty_owned(fd))
    return 0;

  /* Terminal negotiation belongs to axil-tty, which owns the echo policy: it
   * states WILL ECHO once per socket (guarded by its own echo_sent) and again
   * when a PTY is born. This used to be open-coded here, deciding WILL vs WONT
   * ECHO from DOCUMENT_URI -- which is how the gate above came to be, and how
   * the negotiation drifted from what the PTY would later claim. */
  axil_tty_attach(fd);

  unsigned player_ref = nd_connect(fd);
  /* nd_connect() -> auth() -> nd_player_login() returns TWO distinct failure
   * sentinels, not one: plain 0 when there is no REMOTE_USER at all
   * (mcp_auth_fail already emitted), and NOTHING when nd_player_login's own
   * `if (axil_auth(fd, user)) return NOTHING;` (world.c) rejects the name --
   * which getpwnam() does for any site-registered account that is not also
   * a real OS/passwd-backed user (confirmed: an axil-auth-registered name
   * resolves through the site's own login flow, not through getpwnam()).
   * NOTHING is (unsigned) -1, nonzero, so the old `!player_ref` check alone
   * let it through: this then called nd_io_attach(fd, NOTHING) below,
   * clobbering the valid attach nd_player_login's own NEW-PLAYER branch had
   * already made (world.c, nd_io_attach before the axil_auth check) --
   * and any later command on this fd (do_say and siblings read
   * eng_fd_player(fd) unconditionally) aborted in corm_get_copy on the
   * bogus key. The raw-telnet counterpart (world.c:do_connect) already
   * checks both sentinels; this WS path just didn't.
   *
   * Note this means axil_auth()'s rejection of an unknown-OS name does not
   * actually block a returning site-registered player from being usable
   * here: nd_player_login's own earlier nd_io_attach (for a brand-new
   * player) or its RETURNING-player branch's nd_io_attach already set a
   * working fd->player mapping before the rejection fires, and this check
   * now simply stops clobbering it rather than making nd_player_login's
   * rejection fully authoritative. Whether axil-nd should enforce that
   * rejection all the way through (disconnecting an unknown-OS name
   * outright) is a separate policy question, not addressed here -- this
   * fix's scope is the crash only. */
  if (!player_ref || player_ref == NOTHING) {
    /* A decline is invisible to axil's upgrade path -- axil_ws_upgrade takes
     * no teardown action when no hook claims the connection -- so without
     * this the descriptor and its TCP socket stay open indefinitely (the
     * pre-fix behaviour: `ws_init` in the log, then silence, no disconnect
     * ever). The client that probed it held a live, silent connection for
     * its whole timeout, one fd per probe. Close it: the close frame first
     * (a spec-correct peer then terminates its own side), then the full
     * teardown so the socket does not linger for peers that ignore the frame.
     * axil_close() inside this hook is safe: it runs the disconnect hooks
     * (nd's own nd_disconnect tolerates a missing entry, axil-tty's tolerates
     * a missing pty since S5.4) and the post-hook `d->flags |= DF_CONNECTED`
     * lands on a zeroed slot that descr_new() memsets on reuse.
     *
     * The reachable case is an unauthenticated /nd upgrade: no REMOTE_USER, so
     * auth() already sent mcp_auth_fail and returned 0. */
    axil_ws_close(fd);
    axil_close(fd);
    return 0;
  }
  nd_io_attach(fd, player_ref);

  nd_demo_announce(player_ref);
  nd_event_announce(player_ref, player_ref); /* P5: real location */
  /* The ioc dedup self-flushes on every differing write, so the ONLY
   * pending buffer here is the just-buffered on_enter frame. Flush it
   * inside the hook (eng_nd_flush -> ioc_flush -> axil_write) instead of
   * relying on axil's post-command tail-flush, which is timing-invisible
   * to clients. */
  nd_flush(player_ref);
  return 0;
}

XY_IMPL(int, on_axil_parse,
    socket_t, fd,
    unsigned char *, input,
    int, nread)
{
  /* Password-prompt state comes first: while ND_PWPEND is armed the next line
   * is a password, not a command, on either transport. Consumed here (return
   * -1) so it never reaches cmd_parse, the PTY, or the game. */
  {
    char pending[64];
    if (axil_env_get(fd, pending, sizeof(pending), "ND_PWPEND") == 0 &&
        *pending) {
      /* Bounded copy up to the first line end, skipping telnet negotiation
       * (the WILL ECHO sent with the prompt can draw IAC DO ECHO replies that
       * must not become password bytes). */
      char line[128];
      size_t o = 0;
      int i = 0;
      while (i < nread && o + 1 < sizeof(line)) {
        unsigned char c = input[i];
        if (c == '\r' || c == '\n' || c == '\0')
          break;
        if (c == 255 && i + 1 < nread) { /* IAC */
          unsigned char opt = input[i + 1];
          if (opt == 250) { /* SB: skip to IAC SE */
            i += 2;
            while (i + 1 < nread &&
                   !(input[i] == 255 && input[i + 1] == 240))
              i++;
            i += 2;
          } else if (opt == 251 || opt == 252 || opt == 253 ||
                     opt == 254) { /* WILL/WONT/DO/DONT + option */
            i += 3;
          } else {
            i += 2;
          }
          continue;
        }
        line[o++] = (char)c;
        i++;
      }
      line[o] = '\0';
      nd_password_line(fd, pending, line);
      /* Scrub the secret from our own buffer before returning. */
      memset(line, 0, sizeof(line));
      return -1;
    }
  }

  /* axil-tty's route: shell bytes, not a command stream. Returning -1 keeps
   * them out of cmd_parse entirely -- the axil_tty_active() check below only
   * covers the window after a PTY exists, and on this route there is one from
   * on_axil_connect onwards. axil-tty's own on_axil_parse still gets the
   * bytes and writes them to the PTY; the gate only stops the game. */
  if (nd_tty_owned(fd))
    return -1;

  /* WebSocket frames are decoded by axil ws_read and delivered as complete
   * frame payloads — pass directly to axil's native cmd_parse pipeline. This
   * branch used to be dead code: axil discarded each frame, and this module
   * claimed the descriptor with axil_fd_watch() to read and dispatch the frames
   * itself. Neither the claim nor the hand-rolled dispatch is needed now. */
  if (axil_flags(fd) & DF_WEBSOCKET) {
    /* A live PTY owns the bytes: axil-tty wrote them to the PTY from its own
     * first-refusal hook and returned -1 there, so this never runs for them.
     * Returning -1 keeps axil from also reading them as a command line. */
    if (axil_tty_active(fd))
      return -1;

    /* Start a fresh output cycle for this line. axil's cmd_proc() only calls
     * the axil_command hook -- which lands in nd_command(), and with it
     * nd_io_reset() -- when the verb is a REGISTERED one, and falls through to
     * axil_vim otherwise. The old hand-rolled dispatch called nd_command()
     * unconditionally, so an unknown verb used to reset the ioc too; unknown
     * verbs are exactly character-by-character input, i.e. most of what a
     * player types, and the accumulator is what the dedup flushes against.
     * Without this, that reset would be lost for most keystrokes. */
    nd_io_reset(fd);

    return nread;
  }

  /* HTTP requests pass through untouched -- no telnet scan, no slide, no RAW
   * classification. Everything below assumes a terminal byte stream, but a
   * request body may legally contain 0xFF: axil_tty_input() would read it as
   * IAC, the slide would delete the request head sitting in front of it, and
   * the RAW check would answer a POST with the telnet banner (SECURITY.md
   * S5.5). A raw stream never opens with "METHOD SP", and WebSocket payloads
   * are handled above, so this changes nothing for either of them. */
  if (is_http_method((const char *)input))
    return nread;

  /* axil-tty gets first refusal on the raw path too. Two reasons: strip_telnet()
   * below discards the NAWS payload, and a raw telnet guest negotiates its
   * window size on connect, before any `sh` — same as a WS client. It also
   * writes to the PTY for us, which returning -1 above never did, so a
   * live-PTY guest's keystrokes were being dropped here. */
  int used = axil_tty_input(fd, input, nread);
  if (used < 0)
    return -1; /* a live PTY took the whole line */

  /* axil's descr_read() treats this hook's return as a SKIP FLAG only:
   *     if (axil_parse && axil_parse(fd, input, ret) < 0) return 0;
   *     return cmd_parse(fd, (char *)input, ret);
   * The length returned here is discarded, and cmd_parse re-reads all `ret`
   * bytes from the head of `input`. So a shorter return cannot keep telnet
   * bytes out of the command stream: they stay at the front and cmd_parse
   * sees "\xff\xfa\x1f..." where it expects a verb, which is why a raw guest
   * that sent NAWS on connect could not run `sh` at all. Slide the remainder
   * down over the consumed options and blank the vacated tail with newlines
   * (harmless empty lines for a line-based parser), so the bytes cmd_parse
   * actually reads are the command alone. */
  if (used > 0) {
    int rest = nread - used;
    memmove(input, input + used, (size_t)rest);
    memset(input + rest, '\n', (size_t)used);
    nread = rest;
  }

  int len = strip_telnet(input, nread);

  /* Raw telnet path.  WS clients always open with an HTTP request line
   * (method SP path), so the first NON-HTTP line on a non-WS fd
   * unambiguously marks a raw client — negotiate + greet once, then let
   * cmd_proc dispatch (any unauthenticated raw command is dropped by the
   * auth gate except the CF_NOAUTH "connect").
   * Negotiation status is tracked per-descriptor via axil_env_put/get
   * (backed by d->env_hd, freed automatically on close). */
  char nego[8] = {0};
  if (len > 0 && !is_http_method((const char *)input) &&
      axil_env_get(fd, nego, sizeof(nego), "RAW_NEGO") != 0) {
    axil_env_put(fd, "RAW_NEGO", "1");
    /* Same negotiation the WebSocket path gets from on_axil_connect, from the
     * same place, because axil only runs that hook for a WebSocket upgrade and
     * a raw telnet guest never sees it. The banner is player-facing UX, so it
     * stays here even now that it no longer rides along with a re-send. */
    axil_tty_attach(fd);
    axil_write(fd, "Connect with: connect <name>\n",
               strlen("Connect with: connect <name>\n"));
  }

  return len;
}

XY_IMPL(int, on_axil_command,
    socket_t, fd,
    int, argc,
    char **, argv)
{
  /* Gate, not just on_axil_parse: on a /tty socket there is a window between
   * the upgrade and the first NAWS in which the PTY does not exist yet and
   * axil-tty's axil_tty_input() returns "all options consumed" instead of -1.
   * axil would then run cmd_parse on shell bytes, and a registered verb would
   * land here. */
  if (nd_tty_owned(fd))
    return 0;
  nd_command(fd, argc, argv);
  return 0;
}

XY_IMPL(int, on_axil_vim,
    socket_t, fd,
    int, argc,
    char **, argv)
{
  if (nd_tty_owned(fd))
    return 0;
  nd_vim(fd, argc, argv);
  return 0;
}

XY_IMPL(int, on_axil_disconnect, socket_t, fd)
{
  /* No player was ever created on a /tty socket, so nd_disconnect() would be a
   * no-op -- but gating keeps that true by construction rather than by a
   * lookup that happens to miss. */
  if (nd_tty_owned(fd))
    return 0;
  nd_disconnect(fd);
  return 0;
}

XY_IMPL(int, on_axil_update, unsigned long long, dt)
{
  /* axil's timestamp() is microseconds (libaxil.c:1436) and dt is the
   * difference between two of them (libaxil.c:1673), which is exactly the
   * contract ndc_update() consumed in the reference tree. The previous
   * dt * 1000 treated microseconds as milliseconds and ran the world 1000x
   * fast. */
  nd_update(dt);
  return 0;
}

XY_IMPL(int, on_axil_exit, int, i)
{
  (void)i;
  close_all(0); /* save + close maps + mod_close; no process exit */
  return 0;
}

/* The content-load window. axil fires this once from axil_init(), after -C
 * has done its chroot()/chdir() and before the first bind -- so a file these
 * loaders resolve is resolved inside the jail, and no request can arrive
 * before they finish.
 *
 * Both used to run pre-chroot (st_init() at the tail of nd_world_init(),
 * nd_mods_load() in xy_install() below), and both are exactly the two things
 * that open FILES rather than merely registering: the persisted planet
 * modules and the flat mods.load list. Everything that only registers --
 * engine boot, deps, commands, handlers, the DB open -- stays where it was,
 * because none of it needs the jail and moving it would risk a boot that
 * works today.
 *
 * Order is the old one: st_init() first (it used to end nd_world_init()),
 * then the list. The guard makes a second fire -- a host that calls the hook
 * again, or an exit-time re-entry -- a no-op rather than a double load. */
XY_IMPL(int, on_axil_post_chroot, void)
{
  static int loaded;

  if (loaded)
    return 0;
  loaded = 1;

  st_init();
  nd_mods_load();
  return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

/* Compiled-in default (AXIL_HTDOCS, set above) plus a process-environment
 * override, copied from axil-tty's serve_htdocs() (libaxil-tty.c). A
 * distinct env var name (AXIL_ND_HTDOCS, not axil-tty's AXIL_HTDOCS) is
 * required: both modules load into the same process when the site embeds
 * this engine, and they serve two different asset trees. Deliberately not
 * read from the per-connection request env (axil_env_get()), which is
 * populated from client input. */
static void
nd_serve_htdocs(socket_t fd, const char *file)
{
  char htdocs[PATH_MAX - 1] = AXIL_HTDOCS;
  char path[PATH_MAX];
  const char *override = getenv("AXIL_ND_HTDOCS");
  if (override && *override)
    snprintf(htdocs, sizeof(htdocs), "%s", override);
  snprintf(path, sizeof(path), "%s/%s", htdocs, file);
  axil_sendfile(fd, path);
}

static int
handle_nd(socket_t fd, char *body)
{
  (void)body;
  char key[ENV_VALUE_LEN] = {0};
  if (axil_env_get(fd, key, sizeof(key), "HTTP_SEC_WEBSOCKET_KEY") == 0) {
    if (axil_ws_upgrade(fd) < 0)
      return 1;
    /* No axil_fd_watch() here on purpose. It used to be described as mandatory,
     * because axil did not route frames to on_axil_parse and the only way to
     * see a client frame was to claim the descriptor and read it from
     * axil_fd_tick. axil now decodes each frame and delivers the payload to
     * on_axil_parse itself. A claim is not merely redundant, it is destructive:
     * DF_EXTERN takes the descriptor out of descr_read(), so the hook above --
     * which now does all of this module's input handling -- would stop being
     * called entirely. */
    return 0;
  }
  nd_serve_htdocs(fd, "index.html");
  return 0;
}

/* ------------------------------------------------------------------ */
/* module entry point                                                  */
/* ------------------------------------------------------------------ */

void
xy_install(void)
{
  /* boot the real engine first: opens the store, seeds the world,
   * registers the SIC adapters (mod_load_all only on a live db). */
  if (nd_world_init(0, NULL))
    fprintf(stderr, "nd_world_init failed\n");
  nd_register_commands();
  axil_register_handler("GET:/nd", handle_nd);
  /* axil-tty's route, registered here instead of relying on the xy_load()
   * below installing it as a side effect. axil_register_handler() is
   * last-wins, so xy_load() registering it again a line later is a no-op with
   * the same function. What xy_load() is still needed for is its hooks:
   * axil-tty's on_axil_connect opens the PTY and sets auto_shell, and its
   * on_axil_parse/on_axil_tick are what make a shell live here at all. */
  axil_register_handler("GET:" ND_TTY_ROUTE, axil_tty_handle_tty);
  /* nd_mods_load() and st_init() are NOT here: they open files (the list,
   * the persisted modules), so they run from on_axil_post_chroot() above --
   * after -C's chroot, before the first bind. Everything here registers
   * rather than resolves, so it stays pre-chroot where it has always run. */
  xy_load("axil-tty");
  /* Passworded connect authenticates through axil-auth's exported credential
   * check. The bus convention (non-zero means valid) already fails closed when
   * the module is absent; the flag only decides the error message. Same module
   * name the site uses (mods/auth). */
  if (xy_load("libaxil-auth") != 0) {
    fprintf(stderr, "axil-nd: libaxil-auth unavailable; "
                    "passworded connect disabled\n");
    nd_set_auth_ready(0);
  } else {
    nd_set_auth_ready(1);
  }
}

/*
 * Engine provider TUs — folded in so libaxil-nd.so is ONE translation unit.
 * (Both TUs were including ttypt/xy-mod.h, each defining its own
 * `static struct xy_ctx xy` + weak get_xy_ptr(); only one of the two copies
 * ever received the injected module context (xy.load etc.), so the other
 * TU's XY_CALL/xy_load dispatches crashed on a NULL function pointer.
 * One TU  ==  one module context  ==  one injection target.)
 */
#include "nd_xy.c"
#include "nd_events.c"
#include "nd_api.c"