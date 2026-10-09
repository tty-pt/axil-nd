# axil-nd

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![MUCK](https://img.shields.io/badge/game-MUCK-4B8BBE)](#)

> NeverDark MUCK engine as a dynamic [axil](https://github.com/tty-pt/axil)
> module — the game runs in-process behind axil's WebSocket/telnet server.

A port of the [NeverDark](https://github.com/tty-pt/neverdark) MUCK (a
rewrite of TinyMUCK/FuzzBall) built on the axil HTTP/server stack and the
corm database:

- Game engine + region-modded content modules in-process (`loadmod`/
  `unloadmod` into persisted `st` rows, restored every boot)
- WebSocket + telnet protocol served by axil (`GET:/nd` upgrade)
- Store ported from the legacy `qdb`/`qmap` API to `libcorm`
- Browser client (xterm 6) plus `art/` and the in-game `man/` help pages,
  all installed under `share/axil-nd`

## Status

Playable. The engine port is complete and the tree is self-contained: it
builds, tests and runs from a fresh clone with no other checkout required.

## Build from source

The module builds with a plain `make` (the shared [`mk` include.mk]
(https://github.com/tty-pt/mk) is expected as a sibling directory, as is
`axil-tty` for its headers and `libaxil-tty`).

The browser client is **generated** and gitignored, so build it first — a
plain `make` alone leaves `htdocs/` empty and the client 404s:

```sh
npm install           # install build deps
npm run build         # generates htdocs/ (the client)
make                  # builds lib/libaxil-nd.so and generates man/
make test             # run the in-tree test suite (./test.sh)
sudo make install     # module + htdocs + art + man -> $(PREFIX)/share/axil-nd
```

**Dependencies:** `libaxil`, `libcorm`, `libxylem`, `libislet`, `libqsys` and
`axil-tty` (from the tty.pt repo) provide the headers and libraries. All six
are required; the CI dependency list must match `LDLIBS` in the `Makefile`.

## Quick start

The module resolves `art/`, `htdocs/`, `man/` and `game/` **relative to the
working directory**, so run it from the checkout root (or the installed
`share/axil-nd`):

```sh
axil -d -A -p 8080 -m ./lib/libaxil-nd.so
```

Then play at <http://localhost:8080/nd>, or over telnet:

```sh
nc 127.0.0.1 8080
connect <your-unix-username>
```

The guest name must be a real account on the host — authentication calls
`getpwnam()` and fails otherwise. Over raw telnet, terminate every command
with a blank line, as the reader only dispatches a complete head.

On a development host, point the store somewhere writable:

```sh
mkdir -p var
AXIL_ND_DB=./var/std.db axil -d -A -p 8080 -m ./lib/libaxil-nd.so
```

## The man pages

The 52 in-game help pages (`help <topic>`) are tracked as `man-src/*.10` and
copied into `man/` by the `man` target, which `all` depends on. `man/` is
gitignored build output and is resolved relative to the working directory at
runtime, exactly as `art/` and `htdocs/` are.

## License

BSD 2-Clause License. Copyright (c) 2026, quirinpa. See `LICENSE`, which also
carries the TinyMUCK/FuzzBall GPL lineage note. Components retained from the
original TinyMUCK/FuzzBall codebase remain under the GPLv2; the inherited
`GPL.txt` and `docs/COPYING` are included verbatim in this repository.
