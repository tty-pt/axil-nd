#ifndef AXIL_ND_H
#define AXIL_ND_H

/**
 * @file axil-nd.h
 * @brief NeverDark MUCK engine as an axil module.
 *
 * Loads the NeverDark (TinyMUCK/FuzzBall fork) world into axil and serves the
 * browser client + game protocol under `/nd`: a `GET:/nd` WebSocket upgrade
 * handler, the game's verb command table, telnet negotiation, and static
 * client assets from `share/axil-nd/htdocs`. Dispatch happens through the
 * axil xy bus (`on_axil_*` hooks).
 *
 * The server-side data store is corm; the legacy qdb/qmap API from the
 * original engine is bridged by an in-tree compat shim (see src/qdb.c).
 */

#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>

#endif