## 1.1.0

The terminal is gated, the region system is real, and the store survives
shutdown. 11 commits since 1.0.0; the suite gained S5.4/S5.5, S6, S7, S8 and
S9 and the site gained an e2e round-trip.

Changed
- **`connect` is passworded; a name alone never authenticates.** Two phases:
  `connect <name>` arms a one-shot password prompt on that descriptor and the
  next line is the password (consumed as a password, never as a command, and
  never echoed back). The check goes through axil-auth's exported
  `auth_password_matches`, unknown user / wrong password / unconfirmed account
  answer one uniform `Invalid name or password.` so the prompt cannot be used
  to enumerate accounts, and a failed guess costs a reconnect. The old
  passwordless form is gone. Refusal of the terminal itself happens in
  axil-tty: a non-zero `axil_tty_shell(fd)` is the gate bit, and no PTY is
  born before it.
- **Site login completes, and declined `/nd` upgrades are closed** (G4).
  `nd_player_login` no longer reads `axil_auth()`'s advisory "no passwd entry"
  return as an auth rejection — every axil-auth-registered account has no
  passwd entry, so logins bailed before `mcp_auth_success`/actions/view; the
  descriptor is already authenticated and `REMOTE_USER` is what established
  the identity. A declined upgrade (no `REMOTE_USER`) now sends the close
  frame and tears the descriptor down, where it used to sit open with nothing
  on the wire after `AUTH_FAILURE` — one lingering fd per probe.
- **One region selector dialect and a default target region** (CMD_REGION.md).
  Seven commands can name a region: six now share `st_cmd_region()` and
  `wall`, which had a private all/world dialect and bypassed the shared parser
  with no argument, takes the same bare-world-number-or-`cosmos` token at a
  fixed argv position. One default resolution everywhere — explicit selector
  → default target → position — with the default stored in ENT as
  `(target_id, target_plen)` by the new `target` command (ENT grows 16 → 24
  bytes; `ST_SEL_UNSET` is the unset sentinel, never returned). `do_wall`
  parses its own leading token so `wall 3` is still the message 3.
- **`EF_WIZARD` is gone; region ownership replaces it** (ST.md §27.6(1)). The
  flag was never set by any path in the port, so every `st_is_wiz()` gate was
  dead — the commands existed and no player could reach them. The concept is
  removed rather than granted, in two behaviour-proving steps: first
  delete-and-replace to prove the reads were inert, then open the nine sites.
- **The legacy `st_run`/`sl` dlopen path is retired.** `eng_st_run`,
  `st_open`/`st_put`/`st_dlclose`, the `st_get`/`st_key` helpers, the `sl`
  table, the `st_run` PAPI/XY slot and the `stchown`/`streload` commands are
  deleted: symbol dispatch is hook dispatch (`nd_scope_dispatch` walk),
  per-shift ownership is per-region ownership (`st_can` on id/plen rows) and
  per-shift reload is `loadmod`/`unloadmod` over the persisted set.
- **`npm install` is JS-only.** The root `postinstall: make` hook is gone, and
  `@tty-pt/axil-tty` is bumped to 1.3.2, which drops its own: installing ran
  `make` inside `node_modules/@tty-pt/axil-tty`, whose Makefile gets every
  rule from `-include ./../mk/include.mk` — a path that only exists as a dev
  sibling checkout, never under `node_modules` — so a plain `npm i` died with
  `make: no target to make` (prod hit exactly that). The C side is the
  system-installed axil stack and the checkout's own `make`, which is what
  README "Build from source" documents anyway; nothing consumed the
  node_modules build.

Added
- **Region primitives** (NO_WIZ.md §4): `st_region_covers()` (pure
  prefix-containment identity, cosmos included — `plen == 0` masking yields 0,
  so the root covers everything with no special case), `st_in_scope()` (the
  default-scope engine: containment with an explicit selection, the union of
  the actor's rows without one — deliberately not `st_can_region`, which would
  let one planet claim the address space through the cosmos fallback),
  `st_region_of_obj()` (walk containment up to the first `TYPE_ROOM`, depth-capped
  at 8) and `st_can_region()` un-static'd as one loop over the candidate
  ancestor widths `{0,16,32,48,64}`. An unmapped room resolves to the void,
  `(0,0,0,0)`, which is *in* the cosmos — players log into unmapped rooms, so
  "unresolvable" would put them under nobody's authority; `NOTFOUND` is
  reserved for a genuinely dead-ending walk.
- **Region-owned commands and a region ban table** (ST.md 27.6(1)). Every
  refusal now names the region that refused it via `st_region_ruler` +
  `st_refuse_region`, so a non-ruler learns who to ask instead of getting
  silence. `eng_controls` is the four-case rule — possession stays
  authoritative with one exception (a region ruler may move a player standing
  in their region), rulership is the authority over unowned things — and
  `eng_look_at` lets a ruler inspect entities in their region. Bans are a
  separate corm table keyed `(player, region id, plen)`, enforced only in
  `eng_enter`, the one chokepoint every arrival passes through: the refused
  mover stays put, is told which region refused them, and cannot route around
  it. Login is deliberately not refused.
- **Room persistence**: carved rooms are permanent (`RF_TEMP` cleared) so
  `eng_room_clean` does not collect an empty room (§27.3), and the room is
  reported back (`room %u at %d %d %d %d (existing)`).

Fixed
- **`teleport #<ref> here` moved a bystander — or killed the daemon.**
  `eng_obj_exists()` returned `corm_get(...) == NULL`, so it was true for an
  *absent* row: `eng_ematch_absolute()` therefore discarded every real ref and
  accepted every fabricated one, `teleport #<valid> here` fell through to the
  contents scan and moved the last object iterated in the room (measured: the
  dolphin), and an absent ref reached `corm_get_copy` → SIGABRT from heap
  corruption — any ref a player can type. `eng_ematch_at()` additionally
  returned `tmp_ref` unconditionally ("no match" surfaced as "some bystander"),
  reassigned `where_ref` (the range key) mid-iteration, and leaked the
  iterator. Separately `do_teleport()` never called `eng_nd_flush()`, so every
  refusal and the victim's confirmation sat in the per-fd buffer until the
  player typed again — the whole reason the command "wrote nothing". All six
  return paths flush now, and `argv[1]` is guarded on the string rather than
  `argc` (axil delivers a bare verb as `argc == 2` with an empty `argv[1]`).
- **The shutdown store truncation that read as a persist flake.** libcorm
  saves every file-backed map from a destructor at process exit, so
  `close_all()` closing the maps first made the shutdown save recompute the
  store's size from what was left in corm's file cache and rewrite the file at
  that size — 16 bytes with nothing left. The damage varied per run
  (16/1184/1600/2000/2400/3200 bytes), which is why it read as the old
  nondeterministic flake, and it became destructive the moment one map was
  left open — which is why it looked like a ban-table bug. Measured chain: a
  correct 8461-byte image, the next SIGTERM rewrote it as 3200 bytes of seeds,
  the following boot restored nothing. Every path into `close_all()` exits
  anyway, so it no longer closes anything.
- **Stale contents pairs no longer delete the player.**
  `eng_object_move(old, NOTHING)` deleted every row its `contents_hd` listed
  without checking, so a stale pair aborted a boot by deleting the player out
  from under the session (`corm_get_copy: no record`). Collect-then-delete:
  gone rows, rows filed elsewhere and self-pairs drop the pair, not the object.
- **Two memory bugs ASan found**: `noise.c spread()` ran `p <= nx * CHUNK_M`
  while each iteration writes two `CHUNK_SIZE` lines, so the last `memcpy`
  started 2432 bytes past `chunks_bio` and overwrote the bio pointer on every
  player login; `view.c biome_bg()` copied 16 bytes out of 6-byte `ansi_bg`
  literals and indexed them with an unvalidated `biome_skel->bg` from a row a
  missing skel leaves uninitialized.
- **`eng_ent_get()` zero-initializes an ENT for a missing row**, so an
  authorization path no longer reads fields left uninitialized (this is what
  made `EF_WIZARD` read as reliably-false only by luck), and `do_owned`,
  `do_clone`, `do_create`, `do_chown`, `do_ban` and `do_teleport` validate
  `argc` — the short-circuit in the deleted gates was what kept the NULL deref
  from being live.
- **HTTP request bytes are no longer telnet-scanned** (S5.5). Chunks opening
  with an HTTP request line pass through `on_axil_parse` untouched: no IAC
  scan, no slide, no RAW classification — a `0xFF` body byte used to delete the
  request head and answer the POST with the telnet banner.
- Room-cleanup and login-path guards, plus `test.sh` binding `PATH` and
  `LD_LIBRARY_PATH` to the in-tree build so it tests this checkout instead of
  stale installed copies.

## [1.0.0]

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