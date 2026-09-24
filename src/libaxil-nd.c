#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

/* xy-mod.h must come first so it defines the module xy context used by the
 * XY_CALL dispatches in this translation unit */
#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/corm.h>

#include <arpa/telnet.h>
#include <string.h>

#ifndef AXIL_PREFIX
#define AXIL_PREFIX "/usr/local"
#endif

#ifndef AXIL_HTDOCS
#define AXIL_HTDOCS AXIL_PREFIX "/share/axil-nd/htdocs"
#endif

/* ------------------------------------------------------------------ */
/* axil hook implementations (stub until the engine lands)             */
/* ------------------------------------------------------------------ */

XY_IMPL(int, on_axil_connect, socket_t, fd)
{
  /* initial telnet negotiation — harmless over WebSocket, mandatory over
   * a raw telnet/openssl connection */
  TELNET_CMD(fd, IAC, WILL, TELOPT_ECHO);
  TELNET_CMD(fd, IAC, WONT, TELOPT_SGA);
  TELNET_CMD(fd, IAC, DO, TELOPT_NAWS);
  return 0;
}

XY_IMPL(int, on_axil_parse,
    socket_t, fd,
    unsigned char *, input,
    int, nread)
{
  /* P0 stub: echo WebSocket payloads back over the socket to prove the loop.
   * Raw TCP streams (HTTP requests headed for cmd_parse/do_GET) pass through
   * untouched. */
  if (axil_flags(fd) & DF_WEBSOCKET) {
    axil_write(fd, input, nread);
    return -1; /* consumed; skip cmd_parse */
  }
  return nread;
}

XY_IMPL(int, on_axil_update, unsigned long long, dt)
{
  (void)dt;
  return 0;
}

XY_IMPL(int, on_axil_disconnect, socket_t, fd)
{
  (void)fd;
  return 0;
}

XY_IMPL(int, on_axil_exit, int, i)
{
  (void)i;
  return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

static int
handle_nd(socket_t fd, char *body)
{
  (void)body;
  char key[ENV_VALUE_LEN] = {0};
  if (axil_env_get(fd, key, sizeof(key), "HTTP_SEC_WEBSOCKET_KEY") == 0) {
    axil_ws_upgrade(fd); /* handshake; fires axil_connect() */
    return 0;
  }
  axil_respond(fd, 200, "axil-nd\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* module entry point                                                  */
/* ------------------------------------------------------------------ */

void
xy_install(void)
{
  axil_register_handler("GET:/nd", handle_nd);
}