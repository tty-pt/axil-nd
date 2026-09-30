## 1.0.0

First playable release. The engine port from TinyMUCK/FuzzBall is complete and
the release is self-contained: it builds, tests and runs from a fresh clone
with no external tree.

Changed
- **The module-facing headers install as `<nd/xy.h>`.** They moved from
  `include/papi/` to `include/nd/` (`nd-xy.h` → `xy.h`, `nd-xy-types.h` →
  `xy-types.h`, `nd-hd.h` → `hd.h`, `nd-hooks.h` → `hooks.h`; guards renamed to
  the basename rule) and the Makefile sets mk's `FOLDER := nd`, so
  `make install` puts them in `$(PREFIX)/include/nd/`. They previously went to
  `$(PREFIX)/include/axil-nd/papi/`, a directory no default include path
  reaches, so every out-of-tree module carried a private `-I` to find a header
  the house installs in a plain `<ttypt/…>`-style location. A module now writes
  `#include <nd/xy.h>` and needs no `-I` beyond the one every build already has.
  No forwarding headers: the seven module repos get one line each. `install-papi`
  is now `install-mods` and installs only `nd-mod.mk`, which mk's `share` set
  cannot reach; `make uninstall` now removes it and `include/ttypt/axil-nd.h`.

Added
- NeverDark engine as an axil module: 16 engine TUs linked into
  `lib/libaxil-nd.so`, with `nd_api.c` forwarding the 52 `XY_IMPL` hooks to the
  `eng_*` symbols.
- Raw telnet and WebSocket (`GET /nd`) play paths, with `-A` auto-auth for WS.
- Browser client: xterm 6 terminal, biome background art, day/night, room
  contents panel, equipment and stats modals, item action bar.
- The 52 in-game help pages, tracked as `man-src/*.10` and generated into the
  CWD-relative `man/` that `src/man.c` reads at runtime.
- `make test` target running the in-tree suite (`test.sh`).
- `GPL.txt` and `docs/COPYING`, copied verbatim from the inherited TinyMUCK
  tree, with the `LICENSE` lineage note extended to name both.

Fixed
- `package.json` could not be installed: an invalid `@tty-pt/sub` range
  (`^0.x.x`), a dev/peer skew on `@xterm/*`, and `lodash` missing from
  `external` (which crashed `scripts build`).
- The client template was never wired up: no stylesheet link, no script tag for
  the bundle, and no `#main` / `#title`. The page loaded and stayed inert.
- Four client boot crashes, including `Avatar.setImageClass` dereferencing a
  guarded-against-absent value on the following line.
- Two `??` precedence bugs that made their guards inert.
- CI installed 3 of the 6 libraries the module links.
- The `deb` package could not be built: Tailwind v4's `@tailwindcss/oxide` needs
  Node >= 20, the `ubuntu-builder` image ships Node 18, and npm then silently
  drops the optional native binding. The client CSS is now generated with
  Tailwind v3, which is pure JS and needs no native addon.
- The `brew` packages could not be built: `-Wl,--no-as-needed` and
  `-Wl,--as-needed` are GNU ld options and Apple's `ld` rejects both. They are
  now emitted on ELF targets only.
- The `brew` packages then failed in `install`: `install -D` is GNU coreutils
  only, and BSD `install` has no `-D`. `art/`, `man/` and `nd-mod.mk` are now
  staged with `mkdir -p` plus a plain `install -m 644`.

Removed
- Dead `libnd`-era files: `module.mk`, `ndcc`, `.config`, `axil-cli.js`, and the
  generated `objects-set.mk` (now gitignored).
- Absolute `/home/quirinpa/nd` and `/home/quirinpa/axil-tty` paths from the
  build and the test suite.

## 0.1.0

- Initial scaffold for axil-nd (P0): module build skeleton modeled on
  axil-tty, `include/ttypt/axil-nd.h`, stub `src/libaxil-nd.c` with xy hooks,
  WS upgrade handler `GET:/nd`, `test.sh`.
- License: BSD-2-Clause (quirinpa) with the TinyMUCK/FuzzBall GPL lineage
  note.