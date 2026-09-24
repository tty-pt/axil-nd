# axil-nd

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![MUCK](https://img.shields.io/badge/game-MUCK-4B8BBE)](#)

> NeverDark MUCK engine as a dynamic [axil](https://github.com/tty-pt/axil)
> module — the game runs in-process behind axil's WebSocket/telnet server.

A port of the [NeverDark](https://github.com/tty-pt/neverdark) MUCK (a
rewrite of TinyMUCK/FuzzBall) built on the axil HTTP/server stack and the
corm database:

- Game engine + `mod_load` content-module loading in-process
- WebSocket + telnet protocol served by axil (`GET:/nd` upgrade)
- Store ported from the legacy `qdb`/`qmap` API to `libcorm`
- Browser client + `art/` served from `htdocs/` (installed under
  `share/axil-nd/htdocs`)

## Status

Work in progress — engine port. See `docs/` (todo: `docs/OVERVIEW.md`).

## Build from source

The module builds with a plain `make` (the shared [`mk` include.mk]
(https://github.com/tty-pt/mk) is expected as a sibling directory):

```sh
make                  # builds lib/libaxil-nd.so (and the lib/axil-nd.so link)
make test             # run the in-tree test suite (./test.sh)
sudo make install     # module + htdocs → $(PREFIX), default /usr/local
```

**Dependencies:** `libaxil`, `libcorm`, `libxylem` packages (from the tty.pt
repo) provide the headers and libraries.

## Quick start

```sh
axil -d -A -p 8080 -m axil-nd
```

Open `http://localhost:8080/nd` for the game (client build required; see
`package.json`).

## License

BSD 2-Clause License. Copyright (c) 2026, quirinpa. See `LICENSE` (with the
TinyMUCK/FuzzBall GPL lineage note).