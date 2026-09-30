# MODS.md — porting the old `~/nd` game modules to libxylem

Status: **Phase 0 complete and verified** (0.1-0.4). **Phase 1 ported, loading
and green** — `./test.sh` passes end to end. Two engine bugs were found and
fixed on the way (§6). **Phase 2 planned but not started**: 15 modules
outstanding, sequenced into three waves with a measured dependency graph (§7).
All uncommitted, as of 2026-09-29.

Predecessor: `/home/quirinpa/site/.pi/quest/future/axilnd-modcap-xy.md` (J4.8
step 2) — that quest migrated the *engine's* modding caps to XY. This document
covers the *content modules* it left as "external content modules using `-lnd`
are unverified".

---

## 1. What the old modules are

`~/nd/.gitmodules` points its `mod/` submodule at `tty-pt/nd-basics` (never
checked out — `~/nd/mod` is empty). `nd-basics` is a `.gitmodules` superproject
over 18 `tty-pt/nd-*` repos; `tty-pt/nd-core` is standalone and *not* in the
superproject. 19 modules total.

Every one is the same shape: `main.c` + optional `include/uapi/<name>.h` +
`Makefile`, built by `~/nd/module.mk` with `ndcc`/`-lnd`, exposing
`mod_install`/`mod_open` and `SIC_DEF`/`SIC_DECL` hooks. The engine handed them
the whole API by struct copy: `src/mods.c:_mod_load` did
`struct nd *ind = dlsym(sl, "nd"); *ind = nd;`, then `mod_install` (fresh) or
`mod_open` (known), then the `.sic_auto_init` section.

| module | main.c | implements | notes |
|---|---:|---|---|
| `nd-core` | 877 B | `on_icon` | ported; `struct icon` return across the bus |
| `nd-other` | 477 B | `on_add` | ported; `HD_OBJ`/`HD_SKEL`/`HD_TYPE` |
| `nd-level` | 1015 B | `on_add`, `on_status` | ported; exports `level`/`level_up` |
| `nd-vanilla` | 574 B | `on_new_player` | ported; `on_birth`/`on_death` are non-events |
| `nd-wts` | 513 B | — | no deps; 11× `nd_put(HD_WTS, …)`, no events at all |
| `nd-stone` | 1225 B | `on_add`, `on_spawn` | no deps; `map_where`, `object_add` |
| `nd-biome` | 1291 B | — | no deps; `mod_install` only, `HD_BIOME`/`HD_SKEL` |
| `nd-seat` | 3497 B | `on_add`, `on_before_leave`, `on_examine`, `on_will_attack`\* | needs `fight`; 2 commands, 1 service, 1 `sic_last` |
| `nd-race` | 1786 B | `on_add`, `on_status` | needs `attr`; `sic_last` chain |
| `nd-class` | 1688 B | `on_add`, `on_status` | needs `attr` + `level`; `sic_last` chain |
| `nd-drink` | 3734 B | `on_add`, `on_icon`, `on_view_flags` | needs `mortal`; 2 commands |
| `nd-shop` | 3793 B | `on_icon` | no deps; 3 commands, `nd_iter`, `ematch_at`/`ematch_mine` |
| `nd-mob` | 3609 B | `on_spawn` | needs `fight` + `plant` |
| `nd-attr` | 5518 B | `on_add`, `on_status` | needs `level`; 8 services, 2 commands |
| `nd-mortal` | 6403 B | `on_add`, `on_death`\*, `on_examine`, `on_status`, `on_update` | needs `attr`; 4 services, 3 non-events |
| `nd-equip` | 7874 B | `on_add`, `on_auth`, `on_examine`, `on_leave` | needs `attr` + `fight`; 2 commands, 2 `sic_last` |
| `nd-plant` | 8603 B | `on_add`, `on_empty_tile`, `on_examine`, `on_icon`, `on_noise`, `on_spawn` | needs `drink`; most `HD_*` use (6) |
| `nd-fight` | 9655 B | 9, of which 3 non-events | needs `attr` + `level` + `mortal`; 5 services |
| `nd-spell` | 12410 B | 7, of which 3 non-events | needs `attr`+`equip`+`fight`+`mortal`+`seat`; 2 `sic_last` |

\* **not an event** — no `XY_DEF`, no `nd_evt_*` wrapper. See §7.

**This table is measured, not recalled.** An earlier revision of it was wrong
in three rows, and all three mistakes were in the direction of *inventing
difficulty*: `nd-wts` was listed as 2.2 K with `verb`/`verb_to` (`SIC_DEF`),
`wts_plural` and `nd_iter` — it is **513 B** of `nd_put(HD_WTS, …)` with no
events, no `SIC_DEF` and no `nd_iter`; `nd-race` was listed at 3.0 K with
`nd_assoc` and `vtf_register` — it is 1786 B and calls neither (`vtf_register`
is in `drink`); `nd-fight` was listed at 10.3 K, not 9655 B. `verb_to` and
`ematch_*` do not exist as named — `ematch_at`/`ematch_mine` are in `shop` and
are already in `papi/nd-xy.h`.

The generator was rebuilt from the sources rather than patched, and the
distinction it exposed matters for §7: **`SIC_DEF` does double duty.** A
module's *event* handlers are ordinary functions — only a cross-module *service*
(`fight_damage`, `fighter_wt`, `heal`) needs `SIC_DEF` to be exported, which is
why so many modules have events but no `SIC_DEF` at all. In the new world that
maps to `XY_IMPL` for events and `XY_IMPL` + `XY_DECL` in a module-owned header
for services, which is §6's rule stated the other way round.

Not in scope: the other `tty-pt/*` repos targeting the much older tty.pt module
system (`poem`, `chords`, `sem`, `sb`, `tty-nd`, `commands`, `ndc-cgi`).

---

## 2. Session findings that changed the plan

### 2.1 The build never rebuilt the engine TUs (repo bug, now fixed)

`Makefile` had `-include ./../mk/include.mk` **above** `libaxil-nd-obj-y :=
${ENGINE-obj-y}`. `mk/include.mk:67` writes the library rule as
`$(libtarget): lib ${LIB:%=src/%.o} ${LIB-obj-y}`, and make expands a
*prerequisite list when the rule is read* — at the `-include` — while `${LIB-obj-y}`
was still empty. Recipes expand when they *run*, which is why the engine objects
reached the link line but not the prerequisite list.

Measured before the fix:

```
$ make -p | grep '^lib/libaxil-nd.so:'
lib/libaxil-nd.so: lib src/libaxil-nd.o src/libaxil-nd.o     # 15 engine TUs missing

$ rm src/mods.o && make
cc -o lib/libaxil-nd.so ... src/mods.o ...
ld.bfd: cannot find src/mods.o: No such file or directory
```

So `make` relinked whatever `src/*.o` happened to be on disk. `src/*.o` were
dated **2026-09-28 23:17** while the sources were 2026-09-29. Every "axil-nd ok"
in this session before the fix ran a `.so` built from yesterday's engine.

**Fix:** move the `-include` below `ENGINE-obj-y` / `libaxil-nd-obj-y`. Verified:

```
lib/libaxil-nd.so: lib src/libaxil-nd.o src/libaxil-nd.o src/entity.o ... src/mods.o
```

### 2.2 Consequence: all earlier verification is retracted

The `./test.sh` "axil-nd ok" run in §0.1 of this session is **void**. The only
fresh object was `src/libaxil-nd.o` (it includes `nd_events.c`), so the changes
to `mods.c` and the 20 `SIC_DEF`s were not even in the binary that was tested.
Phase 0 is not verified until a clean `rm src/*.o lib/libaxil-nd.so && make &&
./test.sh` passes. **[Since satisfied — §5.0.1 records the clean-rebuild pass.]**

### 2.3 There are 20 event firing sites, not 19

`view.c:46` (`empty_tile`) called `SIC_CALL(&ss, on_empty_tile, *t, side, ss)`
**directly** instead of going through the `call_on_empty_tile()` inline, so a
`grep -rn 'call_on_'` census missed it. Corrected census: 19 `call_on_*` sites +
1 `SIC_CALL` site = 20, covering all 20 events exactly once each. This is the
one site where the SIC and XY signatures had to be reconciled by hand:
`SIC_CALL` writes through a pointer, the wrapper returns by value.

### 2.4 Two registration paths existed

`world.c:41` defined `SIC_AREG(fname)` and called it 20× (`world.c:417-439`),
registering every adapter explicitly at boot *in addition* to the
`.sic_auto_init` sections that `SIC_DEF` emitted. Both are gone now: `XY_DEF`'s
`AUTO_INIT` constructor is the only registration path. The 20 `SIC_AREG` calls
and the macro are removed.

### 2.5 XY_DECL at the call sites: measured, and the wrappers win

I first chose exported `nd_evt_*` wrappers over `XY_DECL` in `uapi/type.h`, with
two justifications. One does not survive scrutiny, one does — and it is now
**measured** rather than asserted:

- ~~"wrappers preserve the engine's region"~~ — **wrong, and dropped.** `xy_call`
  dispatches from the thread-local current region
  (`libxylem-dispatch.c:213`), and the wrapper runs in the same call with the
  same TLS context as an `XY_DECL` inline would. The module's `xy.call` and the
  global `xy_call` are the same function. There is no region difference.
- **"XY_DECL costs ~84 KB of .data per TU"** — **right, and now verified.** The
  first attempt to measure it returned byte-identical sections because of §2.1
  (nothing had recompiled), so the number was withheld. A full A/B since
  (`/home/quirinpa/axil-nd` = wrappers, `/home/quirinpa/axil-nd-xydecl` =
  `XY_DECL`, both clean rebuilds, `XY_DECL` signatures derived mechanically from
  the `XY_DEF`s so only the dispatch style differs):

  | | wrappers | `XY_DECL` | delta |
  |---|---:|---:|---:|
  | `.so` bytes | 977,056 | 1,744,832 | **+767,776 (+78%)** |
  | `.data` | 542,944 | 1,297,504 | **+754,560** |
  | `.text` | 105,066 | 107,613 | +2,547 |
  | `R_X86_64_RELATIVE` relocs | 294 | 294 | 0 |
  | `./test.sh` | ok | ok | — |

  754,560 / 4192 = **180 adapters = 9 TUs × 20 hooks**. The reloc count is
  unchanged, so the cost is bytes, not load-time fixups.

  Why: `XY_DECL` emits one `static xy_adapter_t` per hook per TU
  (`xy.h:194`), and `sizeof(xy_adapter_t) == 4192` — of which `char ret[4096]`
  (`XY_MAX_RET_SIZE`) is 98%. Every engine call site pays 4 KB for a return
  buffer, and the engine discards most event return values.

  All 20 events *do* fit `XY_MAX_RET_SIZE`: the largest return is `sic_str_t`
  at 260 B (`struct bio` 76, `struct icon` 16, `view_tile_t` 80), well under
  4096. So `XY_DECL` is not broken here — merely expensive. Isolated
  micro-measurement of the macro alone: 20 `XY_DECL` in one TU = `.data`
  83,840 = exactly 20 × 4192; 20 plain prototypes = `.data` 0.

  **Decision: keep the wrappers.** Secondary reasons that also survive: the
  engine TUs stay free of `<ttypt/xy.h>` (which drags in `xy-pp.h`, `qsys.h`),
  there is one definition site, and no `static inline on_add` is stamped into
  every engine TU.

  If this ever needs revisiting, the lever is `XY_MAX_RET_SIZE` in libxylem,
  not the call sites — but note `sic_str_t`-returning `on_empty_tile`/`on_vim`
  bound how far it can shrink.


---

## 3. The blocking finding (unchanged)

**The engine's event firing sites went through SIC, not XY.** 19 `call_on_*()`
inlines from `uapi/type.h` dispatched via `sic_call()` → `src/mods.c` →
`dlsym()` over the legacy `mod_id_hd` list. A module loaded by `xy_load` was not
in that list, so **`XY_IMPL(on_enter, …)` in a libxylem module was invisible to
every firing site.** The demo worked only because `nd_events.c:nd_event_announce`
*additionally* fired `on_enter`/`on_new_player` through XY on connect — which is
why it saw a second `on_enter` and why the engine kept its own `on_new_player`.

Other facts that shape the plan (verified):

- `enum hd` lives in `uapi/io.h:8`; the handles are in `nd.hds[]`
  (`world.c:229`). `papi/nd-xy.h:52` deliberately dropped that vtable, and **no
  engine TU calls `nd_get`/`nd_put`/`nd_open`/`nd_iter`/`nd_next`/`nd_fin`**
  (they use `corm_*` directly) — only modules do. Re-pointing those hooks at the
  enum is therefore zero-churn for module code.
- `ent_skel`, `vtf_pond`, `mcp_hp`, `mcp_stats`, `mcp_mp`, `mcp_equipment` are
  **module-local** (locals, or `SIC_DEF`'d in the module and called via
  `call_*`). No engine work; they port to module-local `XY_IMPL` + `XY_DECL`.
- `on_birth`, `on_death`, `on_murder`, `on_will_attack`, `on_mortal_life` have
  **no firing site in the old engine either** (`git grep` at `1985533`). Dead
  before the port — and sharper still, they are not events in *this* engine
  either: no `XY_DEF`, no `nd_evt_*` wrapper (§6).
- Engine TUs **cannot** include `papi/nd-hooks.h`: `papi/nd-xy-types.h`
  redefines `struct icon`/`enum color`/`sic_str_t` that `uapi/*.h` already
  define. Hence the wrapper split in §2.5.
- `mk`'s install rule covers only `include/${FOLDER}` with `FOLDER ?= ttypt`
  (`~/mk/include.mk:19-21,104-108`). **`include/papi/*.h` is not installed**, so
  a sibling `~/axil-nd-<name>` repo cannot compile against the module API yet.
- `mods.load` is **gitignored** (`.gitignore:5`) and `nd_xy.c:152` hardcodes
  `mods/%s/%s`. A sibling-repo layout needs a shipped, tracked list and a
  path-aware loader.
- `world.c` still calls `mod_load_all()`, and `mod_hd` lives in the persistent
  store, so today the *legacy* loader is the re-install path on later boots.

---

## 4. Decisions taken (user, 2026-09-29)

| Question | Decision |
|---|---|
| Repo layout | one sibling repo per module: `~/axil-nd-<name>` |
| Scope | **vertical slice first**, then the rest in batches |
| `HD_*` access | resolve the enum **engine-side** inside the existing hooks |
| Legacy SIC | retire it — "old sic is now xy" |

---

## 5. Phase 0 — engine contract (prerequisite)

### 0.1 Route the 20 firing sites through XY — **DONE and verified**

- `src/nd_events.c`: keep the 20 `XY_DEF`s, add 20 exported `nd_evt_*`
  wrappers (one per event, body = the generated inline), and reduce
  `nd_event_announce()` to `on_enter` only. Rationale in §2.5 and in the file
  header.
- `include/uapi/type.h`: the `SIC_DECL`/`SIC_DEF`/`SIC_CALL` machinery is
  deleted; replaced by the 20 `nd_evt_*` prototypes. `sic_str_t` and the
  vestigial `sic_adapter_t`/`sic_areg`/`sic_call`/`sic_last`/`sic_get`
  declarations stay until Phase 3. This also removes the tentative `on_*_id`
  globals that forced `-fcommon` (re-check in Phase 3).
- Call sites: `call_on_x(…)` → `nd_evt_x(…)` across `object.c`, `entity.c`,
  `spacetime.c`, `world.c`, `look.c`, `noise.c`, `view.c`; plus the one
  by-hand `SIC_CALL` at `view.c:46` → `ss = nd_evt_empty_tile(*t, side, ss)`.
- `src/mods.c`: the 20 `SIC_DEF`s and the `on_*_id` tentative globals are gone.
- `src/world.c`: the `SIC_AREG` macro and its 20 call sites are gone (§2.4).

Verified 2026-09-29, all on clean rebuilds (`rm -f src/*.o src/*.o.d
lib/*.so && make`):

- `make`: 16 objects, 0 errors.
- `./test.sh`: `axil-nd ok` (WS route, static assets, `/tty` isolation, raw
  telnet, persistence two-boot regression).
- Symbols: `nm -D` shows exactly 20 `T nd_evt_*`; `*_sic_adapter` count 0;
  the four `sic_(call|areg|get|last)` globals remain as §5.0.1 intends.
- Exactly one `on_enter` per connection over 3 raw-telnet connections; zero
  `SIC_CALL BAD`, zero `on_new_player` re-fire on re-login.
- **No behavioural regression**: raw-telnet output is byte-identical to `HEAD`
  (diffed modulo digits, in a `git worktree` of `e48f99e` built with the
  explicit per-TU `make src/<tu>.o` loop that §2.1 makes necessary). The
  `on_demo` frame is absent on the raw route **at `HEAD` too** — pre-existing,
  not a regression; `test.sh` asserts that frame only on the WS route, where it
  does fire. Left for §3's dispatch audit.

### 0.2 HD_* indirection — **DONE and verified**

The pre-0.2 state was worse than "modules pass an untranslated handle":
`shared_get`/`shared_put` took a **raw corm handle**, `nd_get`/`nd_put`/
`nd_iter` forwarded the module's argument straight into `corm_*`, and `nd_open`
threw `corm_open`'s result away and `return 0`. So `nd_get(HD_OBJ, …)` read
**corm table 7**, and any module that saved an `nd_open` handle (nd-class,
nd-level, nd-attr) got 0 and read and wrote table 0.

- `include/papi/nd-hd.h` (new): `enum hd` moved here out of `uapi/io.h`, plus
  the module-table tag. Included by *both* `uapi/io.h` and
  `papi/nd-xy-types.h`, so there is one copy of the enum and modules get it
  without touching a uapi header. The header deliberately contains nothing but
  the enum and inline helpers — no type that `uapi/*.h` also defines — because
  `papi/nd-xy-types.h` is exactly the header that collides with uapi.
- Two namespaces in one `unsigned`:
  - `0 .. HD_MAX-1` — an `enum hd`, resolved engine-side through `nd_hds[]`.
  - `ND_HD_MOD | idx` (`0x80000000 | idx`) — a table a module opened, `idx`
    into the engine's registry. The high bit means a module handle can never
    collide with an `enum hd`, whatever `corm_open` hands out. Modules never
    see a corm handle.
- `src/world.c`: `unsigned nd_hds[HD_MAX]` defined and filled in
  `shared_init()`; `hd_resolve()` and `hd_mod_open()` (corm_open + registry
  insert, returning a tag) live beside it. `nd.hds[]` is still filled from the
  same values for the legacy vtable, which Phase 3 deletes.
- `src/nd_api.c`: `nd_get`/`nd_put`/`nd_iter` resolve via `hd_resolve()`;
  `nd_open` returns `hd_mod_open()`'s tag. Out-of-range input in either
  namespace resolves to 0, which `corm_*` reports as a miss.

Verified against `HEAD` (`e48f99e`, built in the §2.1 worktree) with the **same**
new `demo.so`, so the only difference is the engine side:

| | HEAD | with 0.2 |
|---|---|---|
| `nd_open` returns | `0x0` | `0x80000000` (tagged) |
| own-table round trip | **FAILED, then axil died** | ok |
| `nd_get(HD_OBJ, ref=1)` | never reached | `rc=0 name='quirinpa'` |

Pre-0.2 that probe does not merely fail, it takes the process down: `nd_open`
hands back 0, and the next `nd_put` writes through corm table 0. So 0.2 closes
a crash, not just a wrong-answer.

`./test.sh` asserts the tag, the round trip, both out-of-range misses, and
`resolved (ok)` / `NOT RESOLVED (bug)` on the `HD_OBJ` probe. Each assert
pattern is present in `/tmp/axil_test.log` (verified), so none is vacuous.

### 0.3 Module ergonomics — **DONE and verified**

Both are in `papi/nd-xy.h`; the demo exercises both, so a regression fails at
build time.

- `nd_printf(player_ref, fmt, …)` — static inline, `vsnprintf` into a 1 KB
  buffer then `nd_write` with an explicit length. Returns the byte count, and
  a negative value if the format produced nothing, so truncation is visible
  rather than silent. It is an inline, not a hook: it adds nothing a module
  cannot already do with `snprintf` + `nd_write`, and it keeps the formatting
  on the module side of the bus. This is the replacement for `nd_writef` /
  `nd_owritef` / `nd_twritef`, which cannot be hooks at all (`va_list` does
  not survive the arg-struct copy).
- `nd_last(ret)` — the `sic_last()` port, a macro over the injected
  `xy.last` (`xy.h:349` / `xy_ctx.last`, `xy.h:524`). It reads the **injected
  context**, not the global `xy_last`, because a module links no libxylem:
  `xy-mod.h` declares `static struct xy_ctx xy` and the host fills it via
  `get_xy_ptr()`. So `<ttypt/xy-mod.h>` must precede `papi/nd-xy.h` — which
  is §6's porting rule — and missing it surfaces as `xy` undeclared at the use
  site. It is a macro for the same reason: an inline would fail at *include*
  time even if unused. This is what nd-attr's listener chain needs.
- `./test.sh` asserts both the dispatch markers and the `[demo] nd_printf …`
  frame on the wire.

### 0.4 Out-of-tree build + install — **DONE and verified**

Phase 1 makes four things true that nothing in the engine had ever done: a
module lives in its own repo, builds with no access to the engine tree, is
listed in a tracked file outside the engine's own `mods/`, and is compiled by
`make`. Each of those was broken on the first attempt. The bugs are recorded
below because every one of them failed *silently* — a module that does not load
is indistinguishable from a hook that stopped firing.

**Header install.** `make install` previously installed nothing a module could
compile against: mk's rule only covers `include/${FOLDER}` and `FOLDER=ttypt`
(`mk/include.mk:19-21,104-108`). Added `install-papi`:

- `include/papi/{nd-hd,nd-xy-types,nd-xy,nd-hooks}.h` →
  `$(PREFIX)/include/axil-nd/papi/`, so a module keeps one `-I` and still
  writes `#include "papi/nd-xy.h"`.
- `nd-mod.mk` → `$(PREFIX)/share/axil-nd/nd-mod.mk`.

Verified with `make install DESTDIR=/tmp/ndinst PREFIX=/usr`: all four headers
and the `.mk` land, no errors. `nd-hooks.h` ships even though a module must not
`XY_DECL` from it, because implementing an event needs the canonical signatures.

**Shared build file.** `nd-mod.mk` is one copy of the module build contract, so
it cannot drift from the engine the way per-module copies do. Two decisions
inside it, both forced by failure:

- `ND_INC` is **self-locating**: it inspects the directory `nd-mod.mk` itself
  was loaded from, and uses the sibling `include/` if that is a source tree,
  else `$(PREFIX)/include/axil-nd`. The first version hardcoded the installed
  path, which on a dev host with nothing installed is a directory that does not
  exist — every sibling module died with
  `fatal error: papi/nd-xy.h: No such file or directory`. A module cannot guess
  an unrelated repo's path, so the file that knows has to decide.
- `MOD` defaults to `$(patsubst axil-nd-%,%,$(notdir $(CURDIR)))`, turning the
  repo name `axil-nd-level` into the module name `level`. Without the strip the
  artifact was `axil-nd-level.so` while the engine asked for `level`.
- Both include trees are `$(error)`-checked up front, so a missing one is a
  named make error instead of a confusing "No such file" from the first TU.

**Path-aware loader.** `nd_mods_load()` sends a `mods.load` line containing `/`
to `xy_load()` verbatim and reshapes anything else to `mods/<n>/<n>`. `mods.load`
is no longer gitignored and now ships commented and tracked — a per-checkout
module list is not a thing, and Phase 1 puts a line in it per repo.

Entries name the **stem**, never the file. `xy_load()` appends `.so` itself
(`libxylem-module.c:114`; `libxylem-watch.c:54` notes it strips `.so` back off
for the watch table), so `level.so` looks for `level.so.so`. Measured:

```
mod_load_open_handle: _mod_load failed loading 'mods/demo/demo.so':
    mods/demo/demo.so.so: cannot open shared object file
```

which surfaced only as `FAIL: on_demo frame missing` — no error about the list
file, no error about the path.

**`make mods`.** Builds every module in `mods.load` before boot. Three make
behaviours had to be dealt with, and all three failed as *silent* no-ops:

- `#` cannot appear in a makefile line, not even inside a quoted recipe
  argument: make strips to end-of-line *before* expanding `$$`, so
  `sed -e 's/$$#.*//'` dies at parse time with `Unterminated quoted string` and
  takes every target in the makefile with it. The list is instead extracted by
  matching a name character class at line start, which drops comments, blanks
  and indented lines for free.
- `$(filter %/%,...)` returns the **empty list**. Make's pattern matching is
  not a "contains" operator; measured on GNU Make 4.4.1 with
  `x := a/b demo c/d/e`:

  | pattern | result |
  |---|---|
  | `$(filter %,$(x))` | `a/b demo c/d/e` |
  | `$(filter a%,$(x))` | `a/b` |
  | `$(filter a/%,$(x))` | `a/b` |
  | `$(filter /%,$(x))` | *(empty)* |
  | `$(filter %b%,$(x))` | *(empty)* |

  So no rule is defined and make reports `No rule to make target
  '../axil-nd-<mod>/<mod>.so'`. Replaced with `$(if $(findstring /,...))`.
- A `$(foreach m,...)` variable is only in scope while the **target line** is
  expanded; recipes are expanded lazily, when the rule runs. So `$(m)` in the
  recipe expanded to nothing while the target itself came out correct, and the
  rule died with the memorable `make -f nd-mod.mk MOD= ... -C  .so`. Recipes
  now derive everything from `$@`.

`modnames` is also `$(sort)`ed, because a module listed twice makes make refuse
the whole makefile with `target '...' given more than once in the same rule`.

**Suite.** `test.sh` now builds the engine and every module before booting, and
creates a real out-of-tree fixture (`../axil-nd-testprobe/`, a separate repo
whose only `include` knowledge is `papi/nd-xy.h` and `<ttypt/xy.h>`), registers
it in `mods.load` by path, and asserts it loaded from that exact path and
resolved `HD_OBJ`. The trap restoring `mods.load` is installed *before*
anything can fail; the first version ran `make mods` first, so a build error
left a fixture named in a tracked file and every later run inherited a module
that did not exist.

Adding `make` to the suite was itself a bug fix, found by trying to prove the
new assertion non-vacuous: with the path branch deliberately reverted,
`./test.sh` still printed `axil-nd ok`, because the suite rebuilt modules but
never the library it was about to boot.

**Non-vacuity, measured.** Reverting only the `strchr` branch and rebuilding:

```
FAIL: out-of-tree module was not loaded from its mods.load path
mod_load_open_handle: _mod_load failed loading
  'mods/../axil-nd-testprobe/testprobe/../axil-nd-testprobe/testprobe':
  ...so: cannot open shared object file
```

Then restored: `axil-nd ok`, with `mods.load` back to its shipped contents.

**One real header bug found by the fixture.** `papi/nd-xy.h` called
`vsnprintf` without including `<stdio.h>`. The in-tree demo includes stdio
first and so never showed it; a first out-of-tree module that did not, died
with `implicit declaration of function 'vsnprintf'`. A self-contained public
header has to carry its own dependencies. Also fixed a `/*` inside a block
comment (`uapi/*.h`) in the same header.


---

## 6. Phase 1 — vertical slice (4 modules) — **DONE and verified**

| repo | from | proves | result |
|---|---|---|---|
| `~/axil-nd-core` | `nd-core` (877 B) | `XY_IMPL(struct icon, on_icon, …)` — struct return across the bus | loads, fires on `TYPE_ROOM` |
| `~/axil-nd-other` | `nd-other` (477 B) | `on_add` + `HD_*` indirection | loads, fires, skips as expected |
| `~/axil-nd-level` | `nd-level` (1.0 K) | cross-module `XY_DECL`/`XY_IMPL`, own `nd_open` table, `on_status`, `nd_printf` | loads, `level_hd = 0x80000001 (tagged)` |
| `~/axil-nd-vanilla` | `nd-vanilla` (574 B) | `on_new_player` wiring | loads, fires, teleports ref 1 |

Sources obtained by cloning `tty-pt/nd-basics` (+ submodules) and `tty-pt/nd-core`
into `/tmp`; `~/nd/mod` is an empty submodule. All four build warning-free
through `nd-mod.mk`, and all four load from the tracked `mods.load` by path.

Layout per module (mirrors the old repos, one correction):

```
~/axil-nd-<name>/
  Makefile                 # include /usr/share/axil-nd/nd-mod.mk
  main.c                   # SIC_DEF → XY_IMPL ; mod_install/mod_open → xy_install
  include/<name>/<name>.h  # SIC_DECL → XY_DECL   (NOT include/uapi/)
  LICENSE                  # BSD-2, carried over from tty-pt
  README.md                # build, install, and what the port changed
```

**Correction to this section's original plan:** private headers go in
`include/<mod>/`, not `include/uapi/<mod>/`. `uapi` is engine-owned and already
on the module include path via `ND_INC`, so a module with its own `uapi/` creates
two `uapi` roots — the exact collision §3 exists to prevent. `nd-mod.mk` now
says so too.

### Two things the plan got wrong, found by porting

1. **`on_leave` is in `nd-equip`, not `nd-vanilla`.** The table above originally
   claimed vanilla "proves `on_leave` wiring". It has no `on_leave` at all.
   Verified: `grep -rl on_leave */main.c` in `nd-basics` matches only `equip`.
2. **`ACT_DROP` did not exist in this engine.** `nd-core`'s `on_icon` — the
   function that builds every icon in the game — is unportable without it, and
   `enum base_actions` in `include/uapi/object.h` had only `LOOK`/`OPEN`/`GET`.
   It was lost when the engine was split out of the old nd and never restored.

   Fixed additively as `ACT_DROP = 8`, in **both** `include/uapi/object.h` and
   the module-visible copy in `papi/nd-xy-types.h`. The value is not a guess:
   it is the old engine's own (`/home/quirinpa/nd/include/uapi/object.h:48`), and
   diffing that enum against ours showed `ACT_DROP = 8` was the *only* line
   missing. It is a wire value, not an internal one — `ico.actions` is memcpy'd
   into the BCP frame at `src/mcp.c:116` as a raw int, so the NeverDark client
   already decodes 8. Additive, so no existing value moves.

### Hook audit: all 20 are live, and `on_status` is not dead

An earlier revision of this section concluded that `on_status` was a dead
handler and that the plan's `Level` assertion was therefore impossible. **That
was wrong, and the mistake is worth recording because it nearly caused a real
engine feature to be written off as dead.**

The faulty step was a census that searched for `on_status` in `src/*.c`. The
engine does not call `on_status` by that name — it defines an `XY_DEF` for it
and fires it through a wrapper, `nd_evt_status`, and the wrapper is the name
that appears at the call site. Searching for the event name found only its own
definition, which read exactly like "never invoked".

The correct census is over the wrappers, and it is unambiguous:

```sh
for w in $(grep -oE "nd_evt_[a-z_]+" src/nd_events.c | sort -u); do
	printf '%-22s %s\n' "$w" \
		"$(grep -rlw "$w" src/*.c | grep -v nd_events.c | tr '\n' ' ')"
done
```

**Result: all 20 wrappers have exactly one real call site, in one engine TU
each.** `nd_evt_status` is called from `do_status` (`src/entity.c:236`), which
is the `status` command (`src/world.c:705`). So `on_status` is an ordinary,
live event — it just is not a connect-time one, and the plan asserted its
output against a connect-time capture.

| hook | fired from | | hook | fired from |
|---|---|---|---|---|
| `on_add` | `object.c` | | `on_new_player` | `world.c` |
| `on_after_enter` | `entity.c` | | `on_noise` | `noise.c` |
| `on_auth` | `world.c` | | `on_spawn` | `spacetime.c` |
| `on_before_leave` | `entity.c` | | **`on_status`** | **`entity.c`** |
| `on_clone` | `object.c` | | `on_update` | `object.c` |
| `on_del` | `object.c` | | `on_view_flags` | `view.c` |
| `on_empty_tile` | `view.c` | | `on_vim` | `world.c` |
| `on_enter` | `object.c` | | | |
| `on_examine` | `look.c` | | | |
| `on_get` | `object.c` | | | |
| `on_icon` | `object.c` | | | |
| `on_leave` | `object.c` | | | |
| `on_move` | `spacetime.c` | | | |

**And a second correction, which is the important one for Phase 2:**
`on_birth`, `on_death`, `on_murder`, `on_will_attack` and `on_mortal_life` are
**not events at all**. There is no `XY_DEF` and no `nd_evt_*` wrapper for any of
them, so there is no firing site to find and re-wire — measured, not inferred:

```
$ for h in on_birth on_death on_murder on_will_attack on_mortal_life on_status; do
      printf '%-18s XY_DEF=%s wrapper=%s\n' "$h" \
        "$(grep -c "XY_DEF([a-zA-Z_ ]*, *$h\b" src/nd_events.c)" \
        "$(grep -c "nd_evt_${h#on_}" src/nd_events.c)"
  done
on_birth           XY_DEF=0 wrapper=0
on_death           XY_DEF=0 wrapper=0
on_murder          XY_DEF=0 wrapper=0
on_will_attack     XY_DEF=0 wrapper=0
on_mortal_life     XY_DEF=0 wrapper=0
on_status          XY_DEF=1 wrapper=1
```

They were already unreachable in the old engine, so nothing is lost by not
porting them — but it does mean the §10 question "add firing sites or port them
as documented-dead?" is misframed. There is no firing site to add; the events
would have to be *created*, which is a new engine feature, not a port.
`nd-vanilla` therefore keeps its two dead handlers as commented-out source
(`main.c:59,72`) rather than as `XY_IMPL`s. (An earlier revision cited
`main.c:48,61`, which had drifted to the `on_new_player` body after that file's
header comment was edited — the reference was never re-checked against the
file, which is the same mistake as the census above.)

### Two real bugs the assertions found

Both were found only *after* the assertions passed for the wrong reason, so
both are worth stating as bugs rather than as test fixes.

**1. `make mods` never rebuilt an out-of-tree module.** The per-module rule
took `nd-mod.mk` as its only prerequisite (`Makefile:164`), but nothing in the
engine tree knows a sibling module's sources, so that list could never be
right — and `nd-mod.mk` is older than every `.so`, so make considered the target
permanently up to date and never ran the recipe. Measured, after editing
`axil-nd-other/main.c`:

```
-rw-r--r-- 18:34:18 other.so     <- older than the source
-rw-r--r-- 18:35:14 main.c
```

`make mods` printed nothing for it, the engine loaded the **stale** `.so`, and
the new markers never appeared — so the suite failed on
`"nd-other: on_add first call"` with the source sitting in the log's own repo.
This is §2.1's bug (the engine linking yesterday's objects) one level out, and
the same class: a build that silently ships stale code, which in a modding test
is indistinguishable from a broken hook. Fixed by making the prerequisite
`FORCE` so the recipe always runs and delegates to the module's own
`nd-mod.mk`, which has the real `%.o: %.c` tracking and so still recompiles only
what changed. A/B'd both directions, with a >1 s gap because make treats
equal-second mtimes as up to date:

| | edit `main.c` then `make mods` |
|---|---|
| prerequisite `nd-mod.mk` | `.so` **unchanged** (bug reproduced) |
| prerequisite `FORCE` | `.so` **rebuilt** |

**2. `do_status` never flushed, so a module's status output never appeared.**
`eng_nd_write` is a history+dedup *filter*, not an append buffer (`io.c:180`):
a differing message flushes the **previous** one and replaces it. `do_status`
wrote its own line and then fired `on_status`, so the module's line displaced
it — and then sat pending forever, because nothing flushed it. Verified both
ways, with a temporary `WARN` in the handler proving it *was* entered and
returned the right value:

```
on_status: DIAG on_status entered, player_ref=1 level=3   <- handler runs
Level                                                  <- but no wire output
```

Adding `eng_nd_flush(player_ref)` after `nd_evt_status` fixes it, and that is
the engine's own established pattern — `do_connect` does exactly this after
`nd_event_announce` (`world.c:864-865`), as do `speech.c`, `spacetime.c` and
`nd_xy.c`. `do_status` was simply the one that forgot. A/B'd: without the flush
`status` followed by 2.5 s of silence prints no `Level` and the suite fails;
with it, the line is on the wire immediately.

So `on_status` was never dead and no new event had to be invented. The engine
change is one line, and it makes a feature that has always existed actually
work.

### Porting rules as applied

- `#include <nd/nd.h>` → `#include <ttypt/xy-mod.h>` + `"papi/nd-xy.h"`. A TU
  that `XY_IMPL`s a hook must not `XY_DECL` the same name, so `nd-level` does
  *not* include its own `include/level/level.h` — that is the whole reason the
  public header is a separate file.
- `SIC_DEF(on_x, …)` → `XY_IMPL(int, on_x, …)`; return type from `nd-hooks.h`,
  which is canonical and already differs from `uapi/type.h` in places.
- `SIC_DECL(svc, …)` → `XY_DECL(svc, …)` in a per-module public header.
- `call_x(…)` → `x(…)`; chained listeners use `nd_last()`.
- `mod_install`/`mod_open` → one `xy_install(void)`. `nd-other`'s two functions
  did the same single assignment, so they collapse; `mod_open`'s "already
  installed" case is now just make-idempotence of the install.
- `nd_writef` → `nd_printf` (nd-level's two calls).
- `nd_put(HD_X, …)` / `nd_get(HD_X, …)` → unchanged; §5.0.2 made them resolve
  engine-side.
- Dead handlers → kept verbatim and commented, not ported (`nd-vanilla`).

### Deliberate deviations from a literal transcription

- **`nd-core` had a real bug.** It called `nd_get(HD_OBJ, &what, &what.location)`
  — using `what.location` as the *key* before `what` was ever populated, i.e. an
  uninitialised stack read. That is why its `ref` parameter was marked
  `__attribute__((unused))` while `player_ref`, also so marked, *was* used.
  Ported to `nd_get(HD_OBJ, &what, &ref)`, the evident intent, which also
  removes both stale attributes.
- **`nd-level`'s `on_add`** used a local `unsigned level = 3;`, shadowing the
  `level()` function the same file defines. Renamed to `lvl`.
- **`nd-core` gained an `xy_install`** (it had neither `mod_install` nor
  `mod_open`, relying on SIC's `.sic_auto_init`). It only WARNs.

### `test.sh` additions (as landed)

The `Level\t` assertion is the strongest one, being the only user-visible
output the whole slice produces. It is asserted **after** a `status` command,
not in the connect-time block, because `on_status` is fired by that command and
by nothing else at login. The other four are once-only stderr markers, all on
connect-time events: `nd-core` `on_icon` on `TYPE_ROOM` (that type's value is fixed by the
engine rather than allocated from `HD_TYPE`, so a glyph proves the whole
engine → bus → module → bus → client round trip including the struct return),
`nd-other` `on_add` first call, `nd-vanilla` `on_new_player` teleport, plus a
`level_hd = 0x8…\(tagged\)` regex, a per-module install check, and a
`! grep -qF "failed to load"`.

`nd-other`'s marker fires on the *first call*, not on a match: the engine
creates no object of type `"other"` by itself, so a match-path marker would
never print. What it actually proves is that the hook is wired and the type
comparison behaves (the two values are printed side by side).

---

## 7. Phase 2 — the remaining 15

**15, not the 14 an earlier revision claimed.** 19 modules total (§1), 4 ported
in Phase 1, and the section below names all 15: 4 dependency-free leaves, 6 in
the `attr` stat layer, and 5 above it.

### The dependency graph, measured

Derived from `#include <nd/…>`, not from the old plan's prose, which was wrong
in a way that would have blocked the port on its first attempt:

```
level ─┬─> attr ─┬─> class
       │         ├─> race
       │         ├─> mortal ─> drink ─> plant ─┐
       │         │        └─> fight ─┬─> equip ─┤
       │         │                   ├─> seat  ─┤
       │         └───────────────────┘          │
       └──────────────────────────────────────> │  mob (needs fight+plant)
                                                └─> spell (needs attr,equip,fight,mortal,seat)

leaves, no module deps:  stone  biome  wts  shop
```

The old ordering line (`class → attr → equip → mortal → fight → spell`) put
**three dependents before their own dependencies**: `class` before `attr`,
`equip` before `fight`, and `fight` before `mortal`. It also claimed `mortal`
needs `mcp_bar`, which is true but not a Phase 2 blocker — `mcp_bar` is
*defined* in `mortal` and *used* by `spell`.

### Three waves

**Wave 1 — the four leaves: `stone`, `biome`, `wts`, `shop`.** No module
dependencies, so they can go in any order and in parallel. They exercise the
remaining Phase 0 surface for the first time: `stone` needs `map_where` +
`object_add` and implements two events; `shop` needs `nd_iter`/`nd_next`/
`nd_fin`, `ematch_at`/`ematch_mine`, `action_register`, `nd_register` ×3 and
`on_icon`; `biome` and `wts` implement no events at all, so they only prove
`mod_install` and `HD_*`. Estimated smallest → largest: `wts` (513 B),
`stone` (1225 B), `biome` (1291 B), `shop` (3793 B).

**Wave 2 — `attr` first, then its dependents.** `attr` is depended on by 8 of
the 15 and is the shared stat surface, so its public header is worth
stabilising before anything builds on it: port `attr` alone, freeze
`include/nd/attr.h`'s shape (its 8 exported services — `stat`, `modifier`,
`effect`, `hp_max`, `mp_max`, `attr_award`, `train`, `mcp_stats` — plus the
`enum attribute`/`ATTR_MAX` that its dependents include), and treat later
changes to it as expensive. Then the 6 that depend on it: `class`, `race`,
`mortal`, `drink`, `fight`, and — after `mortal` — `drink`.

**Wave 3 — the five above: `equip`, `plant`, `seat`, `mob`, `spell`.** By this
point every cross-module surface they need exists and has been exercised by a
dependent already, so this wave is the least likely to force a header change.
`spell` goes last: it depends on five modules.

### Three things the old plan got wrong here

1. **`wts` is not a special case.** It is 513 bytes of `nd_put(HD_WTS, …)` and
   is a Wave 1 leaf. The old §7 gave it `verb`/`verb_to` as a `SIC_DEF`
   cross-module service; no such symbols exist anywhere in `nd-basics`.
2. **`race` is not a special case either, and the `nd_assoc` analysis answers
   the wrong question.** `nd_assoc(hd, link, cb)` really is absent from
   `papi/nd-xy.h` — it survives only as a "bring here if a module needs it"
   comment at `nd-xy.h:54`, and the engine body is `shared_assoc`
   (`interface.c:414`). But **no module calls `nd_assoc`**, so whether to add
   the hook is moot. What `race` actually needs is `<nd/attr.h>` and a
   `sic_last(&last)` listener chain in `stat()` — i.e. it is a Wave 2 dependent
   of `attr`, which the old plan never recorded.
3. **The listener chain is 5 modules, not 1.** `sic_last()` appears 7 times
   across `class`, `race`, `seat`, `equip` and `spell` (2 in `equip`, 2 in
   `spell`). The old plan treated the chain as `attr`'s problem alone. It is the
   `attr`→`nd_last()` design that every one of those five depends on, so
   Wave 2's header stabilisation should treat `nd_last()` ordering as a first
   class part of `attr`'s contract, not an implementation detail.

### `on_icon` is the one event with five claimants, and it has no incoming icon

`on_icon` is implemented by **five** modules — `nd-core` (ported) plus `drink`,
`shop`, `plant` and `fight` — and it is the only event with more than one
claimant. Two things about it are not mechanical, and one is not yet settled.

**The signature changed shape, not just types.** The old modules were
*decorators*: `struct icon on_icon(struct icon i, unsigned ref, unsigned type)`
took the running icon and mutated it. The canonical signature is now a
*constructor*: `on_icon(unsigned ref, unsigned type, unsigned player_ref) ->
struct icon` (`nd-hooks.h:35`), with no incoming icon to decorate. So
`shop`/`drink`/`plant`/`fight` cannot be transcribed — each one has to be
re-thought as "produce the icon" rather than "amend the icon", which changes
what happens if two of them run for the same object. `nd-core` happens to suit
the constructor shape (it builds every icon in the game), which is why the
Phase 1 slice did not hit this.

**Registration does not collide — verified.** A temporary second module that
also `XY_IMPL`'d `on_icon` was loaded after `nd-core` in `mods.load`: both
handlers ran (nd-core's once-only marker plus the probe's 4 lines for the same
4 icon events) and the suite stayed green. There is no duplicate-name rejection
and no last-wins-at-registration; the adapters are both live.

**The return is last-wins — MEASURED, and the cause is a missing equivalent.**
A second `on_icon` module loaded after `nd-core` reached `mcp.c` as
`ch='@' actions=0x0 fg=0` — the probe's icon entirely, having clobbered
nd-core's glyph *and* its `ACT_LOOK`. Both handlers ran; only the last one's
return survived.

This is not libxylem being hostile — **it is the absence of the control the old
engine had.** The old `sic_call` iterated a *list* of modules in registration
order, passed every one of them the *same* return pointer, and copied each
module's result back out after every call (`interface.c:378-404`):

```c
while (qmap_next(&mod_id, &ptr, c)) {
        adapter.call(retp, cb, arg);
        adapter.ran++;
        memcpy(adapter.ret, retp, adapter.ret_size);   /* previous result kept */
}
```

`xy_adapter_t` has the *identical* pair of fields — `char ret[]` "Buffer for last
return value" and `unsigned ran` "Number of modules that ran this hook"
(`xy.h:125-129`) — and `xy_call` does pass one shared `retp` to every module
(`libxylem-dispatch.c:296,313`). So the mechanism was carried across.

**What is missing is the read side.** The old engine's `sic_last()` returned the
previous module's result *and* told you whether anything had run
(`interface.c:371-376`). `xy.last()` is the same signature — but in this
libxylem it cannot be called mid-dispatch:

```c
/* libxylem-dispatch.c:9-22 */
if (!xy_last_ran) { XY_SET_ERR(XY_ERR_NOTFOUND); return XY_ERR_NOTFOUND; }
if (ret && xy_last_retp) memcpy(ret, xy_last_retp, xy.adapter->ret_size);
```

and `xy_last_ran` is `0` for the whole dispatch, set to the total only *after*
the loop (`dispatch.c:270` before, `:340` after). So a handler calling
`xy.last()` — i.e. `nd_last()` — always sees `NOTFOUND` and copies nothing.
The return buffer is shared and correct; the guard on reading it is what makes
the chain unobservable. Hence last-wins by default.

**Confirmed on the wire — MEASURED.** A throwaway second `on_icon` module,
loaded after `nd-core`, called `xy.last()` and printed what it got:

```
XYLAST type=1 ref=2 rc=-1 ran=0 ret_size=16 prev{ ch=' ' actions=0x0 fg=0 }  <- UNINITIALISED
XYLAST adapter.ret[0..3]={ 00 00 00 00 }  nd_last() rc via macro = -1 (ch=' ')
```

`rc=-1` is `XY_ERR_NOTFOUND`; `ran=0` even though `nd-core` had already run and
returned a real icon; and `adapter.ret` is all zeros, so it does not even hold
the previous module's value to be read. A decorator has no way to see what it
is decorating. So this is not merely "last-wins by default" — the chain is
genuinely unobservable, and no amount of careful `nd_last()` use in a module
can recover it. Option (a) below is therefore not a preference, it is the only
route that needs no change to another repo. (Probe removed, `mods.load`
restored, no stray `axil`.)

Worse, the guard is not merely useless mid-dispatch, it is *wrong* there: a
handler that triggers a nested dispatch (`look_at`, `nd_get` → any hook) sets
`xy_last_ran` on the way out, so a **later** handler in the outer dispatch can
read a *nested hook's* result and mistake it for its predecessor's.

**This also means §5.0.3's `nd_last()` claim is too strong.** It is called "the
`sic_last()` port" and "what nd-attr's listener chain needs", and Phase 0.3
asserts it on the wire — but that assertion only proves it works *after* a
dispatch, which is not the situation any of the 5 `sic_last()` call sites
(`class`, `race`, `seat`, `equip`, `spell`) is in. Those five are the same
question as `on_icon` and must be treated as unproven, not as done — and the
measurement above says they will in fact read `XY_ERR_NOTFOUND`, so whichever
of them depend on the previous listener's value will misbehave. That is a
Phase 2 dependency worth settling before Wave 2, not a footnote: `class`,
`race` and `spell` are all downstream of it.

**The sibling project already solved this — `~/site/mods` — and it solved it by
never having the problem.** Measured across its 11 modules: **163 distinct hooks,
every one implemented by exactly one module. Zero hooks have two implementors.**
Its dependency style is explicit and one-directional:

```c
/* gig/gig.c */
#include "../index/index.h"          /* relative, §4 sibling layout     */
...
void xy_install(void) {
        xy_load("./mods/index/index"); /* guarantee the callee is there  */
        index_open("Gig", "gig.items", ...); /* ordinary call, not a hook */
}
```

`index_open` is `XY_DECL`'d in `index.h` under `#ifndef INDEX_IMPL` (so the
implementing TU can `XY_IMPL` the same name without colliding — the same rule
§5.0.1 records for `papi/nd-hooks.h`), and `XY_DECL` expands to a
`static inline` that dispatches through `xy_call` to *the* implementor.

So `site/mods` never co-implements a hook: it has one owner, callers name it,
and the callee is reached by an explicit call that carries a **return value the
caller can use** — which is exactly the capability `on_icon` lacks. Where a
consumer needs to *extend* a fixed set, the owner keeps its own ordered table and
the consumer registers into it (`index_module_init()` appends to fixed
`MAX_MODULES` arrays at `index/index.c:28-37`; `source/source.c` does the same
for 63 hooks).

`auth/auth.c:788` even records a cycle being cut —
`/* removed xy_load("./mods/index/index") to break auth<->index cycle; ... */` —
which is the failure mode to watch for: two owners calling each other.

**Applied to nd, this inverts the relationship.** `on_icon` stays singly owned by
`nd-core`, and the four decorators stop being co-implementors: they become
*callers* that get the running icon, amend it, and return it. `nd-core` keeps the
ordered decorator table and walks it exactly as `sic_call` used to, which
restores the old control from inside the model rather than beside it:

- `nd-core` exposes `XY_IMPL(struct icon, on_icon, ...)` — the only implementor;
- it also exposes a registration service, e.g.
  `XY_DECL(int, core_icon_decorate, core_icon_fn, fn)`, in
  `include/axil-nd/core.h` under `#ifndef CORE_IMPL`;
- `shop`'s `xy_install` does `xy_load("../axil-nd-core/core")` then registers
  its amend function;
- `nd-core`'s `on_icon` builds the base icon, then calls each registered
  decorator in registration order, threading the icon through, and returns the
  last — the old `sic_call` semantics, now explicit and ordered.

Each decorator gets its own hook name only if it needs to be *called* by name;
registration is enough for decoration, and registration is what makes the order
visible instead of incidental.

**RESOLVED — implemented as the ~/site/mods style, single owner plus a
decorator table.**

- `~/axil-nd-core/core.h` (new, in the module's own repo): `core_icon_fn` and
  `XY_DECL(int, core_icon_decorate, ...)`, under `#ifndef CORE_IMPL` so the
  implementing TU can `XY_IMPL` the same name — the same guard
  `~/site/mods/index/index.h` uses, and the same rule as `papi/nd-hooks.h`.
- `nd-core/main.c` is the sole `XY_IMPL` of `on_icon`. It holds
  `core_icon_decorators[16]`, and after building the base icon threads it
  through each registered decorator in registration order:
  `i = core_icon_decorators[n](i, ref, type, player_ref);` — the old
  `sic_call` loop, with the order now explicit.
- `~/axil-nd-shop` is ported. Its `xy_install` does
  `xy_load("../axil-nd-core/core")` (relative, §4; `xy_load` is refcounted and
  idempotent via `mod_load_try_reuse_existing`, so it is safe whether or not
  nd-core is also named in `mods.load`) and then
  `core_icon_decorate(shop_icon_decorate)`.
- `core_icon_fn` takes the **running icon as its first argument**, so
  `shop_icon_decorate` is the original `on_icon` body verbatim. This is not
  cosmetic: see the canary defect below. Wave 1 is now **4 of 4**.

**A real defect this surfaced, in the Phase 1 canary.** The suite's only icon
assertion was `grep -F "nd-core: on_icon TYPE_ROOM -> ch='-'"`, and nd-core
printed that `WARN` *inside* its own `on_icon`, **before** the decorator loop.
So the canary could not see the chain it was supposed to be guarding. The first
revision of `shop_icon_decorate` rebuilt the base icon instead of receiving it,
which silently replaced every room's `'-'` with `'?'` — and the suite stayed
**green**, because the marker it was reading had already been printed.

Both halves are fixed. The marker is now emitted *after* the chain, so it
reports the value the client actually received, and `shop` logs on its amend
path (`nd-shop: decorated ref=... ch='$' ...`) so decoration is observable
rather than inferred. Verified by deliberately breaking it: with a decorator
that clobbers, the marker reads `ch='?'` and the suite fails
(`FAIL: nd-core's on_icon did not fire for a room`); restored, it is green.

**Lesson to carry into Phase 3:** a once-only marker printed inside the producer
cannot guard the composition of its output. Every other module's marker in §6
predates chaining and is fine, but anything that runs *after* another module
must be asserted on the post-chain value.

**Options as considered:**

- **(a) Give each decorator its own hook name.** XY hooks are name-addressed,
  so nothing stops `shop` implementing `on_icon_shop` rather than `on_icon`, and
  the single owner of `on_icon` fanning out to each. Uses the framework as
  designed, needs no engine or libxylem change, and keeps each module's
  decoration in its own repo. Cost: the owner needs to know the names, so it
  wants a registry — the same `HD_*`/registry pattern 0.2 already established.
- **(b) Restore the old semantics engine-side.** Have `eng_object_icon` iterate
  registered icon decorators, passing the running icon to each, exactly as
  `sic_call` did. Smallest semantic change, but re-introduces a bespoke
  dispatch path beside XY.
- **(c) Fix it upstream in libxylem** so `xy.last()` is valid mid-dispatch
  (`xy_last_ran` updated per module, and reset per-hook so a nested call cannot
  leak). Correct in the long run and fixes `sic_last` for every consumer, but it
  is a change to another repo and the nested-leak bug has to be fixed too, or
  the fix introduces a worse bug.
- **(d) Fold all four into `nd-core`** and drop the modularity. Cheapest, and
  should be the fallback if (a) turns out to need engine support anyway.

Recommend **(a)**, and treat **(c)** as a separate upstream ticket regardless,
since the nested-leak in `xy.last()` is a real bug for anyone using it as a
`sic_last` equivalent.

**Consequence for the waves.** `shop` was going to be a Wave 1 leaf, and is
still one on dependency grounds, but it is *blocked on this question* rather
than on anything about `shop` itself. `plant` (Wave 3) and `drink`/`fight`
(Wave 2) carry the same exposure, so this is a Phase 2-wide architectural
question that happened to surface in Wave 1, not a `shop` bug.

### Carried forward from the Phase 1 slice

Two things the slice established that apply to all 15, and that are cheaper to
know now than to rediscover per module:

- **A command that fires a hook whose handler writes must flush.**
  `eng_nd_write` is a history+dedup filter, not an append buffer (`io.c:180`):
  a differing message flushes the *previous* one and replaces it, and the last
  one sits pending until something else writes. `do_status` omitted
  `eng_nd_flush` and so a module's status line never appeared at all (§6). Any
  `do_*` handler that fires an event needs the same `eng_nd_flush(player_ref)`
  that `do_connect` does at `world.c:864-865`. Wave 1's `shop` writes from
  `do_shop`/`do_buy`/`do_sell` and `on_icon`, so this is hit immediately, and
  Wave 2's `attr`/`mortal` write from `train`/`heal`/`feed`. **This has not been
  swept across the other `do_*` handlers** — `do_status` was the only one
  fixed, and nothing currently tests for the class of bug.
- **The repo shape is mechanical, but the four existing Makefiles do not follow
  it.** Each new repo needs `Makefile`, `main.c`, `README.md`, BSD-2 `LICENSE`,
  and private headers in `include/<mod>/` — never `include/uapi/`. But all four
  Phase 1 repos do `include /home/quirinpa/axil-nd/nd-mod.mk`, an absolute path
  into one developer's checkout, which breaks for anyone else and defeats the
  self-locating property `nd-mod.mk` was built for. **Fix this in Wave 1**,
  before 15 more repos inherit it; `MOD := <name>` is already correct.

### Non-events, settled

Six of the event names the 15 modules implement have no `XY_DEF` and no
`nd_evt_*` wrapper, so there is nothing to re-wire — they are dead in the old
engine too. Measured by intersecting every `on_*` the 15 define against
`src/nd_events.c`:

| handler | is an event | claimed by |
|---|---|---|
| `on_birth` | **no** | `mortal`, `vanilla`\* |
| `on_death` | **no** | `mortal`, `spell` |
| `on_murder` | **no** | `fight` |
| `on_will_attack` | **no** | `seat`, `fight`, `spell` |
| `on_mortal_life` | **no** | `spell` |
| `on_mortal_survival` | **no** | `spell` |
| `on_mortal_update` | **no** | `fight` |

\* already handled in Phase 1.

**The old plan's table had 5 names; it is 7.** It missed
`on_mortal_survival` and `on_mortal_update`, and it misattributed `on_murder`
to `fight` + `mortal` (only `fight` defines it) and `on_will_attack` to
`fight`/`seat`/`spell` correctly but without noting `on_death` is *also* in
`spell`. All 7 are already unreachable in the old engine, so nothing is lost by
not porting them — but the `on_mortal_*` three are a *family*, so reviving any
of them is a design decision about a new event group, not a wiring fix. Until
that is decided, ship each as commented-out source with a note, the way
`nd-vanilla` does, rather than as an `XY_IMPL` that compiles and never runs.
This is the only thing standing between those ports and seven handlers that
look live and never fire.

### What the engine still owes Phase 2

Measured, and the answer is mostly "nothing":

- **All 12 `HD_*` handles the 15 modules use already exist** in
  `papi/nd-hd.h` — 0.2 covers them. (`HD_ENT` appears in an `equip` comment and
  is not in the old engine's `enum hd` either, so it is not a real gap.)
- Every engine function the 15 call is already exported by `papi/nd-xy.h`:
  `ematch_at`, `ematch_mine`, `ent_get`, `look_at`, `object_add`, `object_copy`,
  `object_move`, `map_where`, `action_register`, `nd_register`, `fd_player`,
  `nd_iter`/`nd_next`/`nd_fin`, `nd_len_reg`, and `nd_printf` for `nd_writef`.
- `struct icon` already carries `.pi` (`nd-xy-types.h:119`), which `shop`'s
  `on_icon` writes, and `biome_skel_t` is already in `nd-xy-types.h:225`.
- **`nd_assoc` is the one real gap**, and no module needs it. Leave it as the
  comment it is.

---

## 8. Phase 3 — retire SIC ("old sic is now xy")

Delete: `src/mods.c`'s sic registry + dlopen loader (`sic_call`, `sic_areg`,
`sic_get`, `sic_last`, `sic_iter`, `sic_next`, `sic_put`, `sic_adapter_t`,
`_mod_load`, `_mod_run`, `mod_load`, `mod_load_all`, `mod_close`); the
`mod_load_all()` call in `world.c`; the `nd.*` vtable fill in `shared_init()`
(`world.c:248-322`); `include/papi/nd.h`'s `struct nd`; the vestigial
fn-pointer globals in `uapi/*.h`; `module.ld`'s `.sic_auto_init`; and
`-fcommon` if §5.0.1's removal of the `on_*_id` globals freed it.

**Behavioural consequence to re-verify:** `mod_hd` lived in the persistent
store, so `mod_load_all()` was the re-install path on later boots. `mods.load`
replaces it; module tables still persist via `nd_open`. Re-run the two-boot-on-
one-store case (`test.sh` persistence regression).

---

## 9. Current tree state (2026-09-29, uncommitted)

Modified (engine): `Makefile` (include-order fix, `make mods`, `install-papi`),
`.gitignore` (un-ignore `mods.load`), `include/uapi/type.h`, `include/uapi/io.h`,
`include/uapi/object.h`, `include/papi/nd-xy.h`, `include/papi/nd-xy-types.h`,
`src/nd_events.c`, `src/nd_api.c`, `src/nd_xy.c`, `src/mods.c`, `src/world.c`,
`src/view.c`, and the `call_on_*` → `nd_evt_*` renames in `entity.c`, `look.c`,
`noise.c`, `object.c`, `spacetime.c`. Plus `test.sh` and `mods/demo/demo.c`.

Untracked and new: this file, `include/papi/nd-hd.h`, `nd-mod.mk`, `mods.load`.
Untracked and pre-existing: `bun.lock`, `std.db` — do not touch those.

Also modified in Phase 1: `src/entity.c` (the missing `eng_nd_flush` in
`do_status` — §6) and the out-of-tree `mods:` rule in `Makefile` (`FORCE`).

State: **Phase 0 complete and verified** (0.1-0.4) and **Phase 1 ported and
verified** — `./test.sh` exits 0, with two engine bugs found and fixed on the
way (§6). Nothing committed.

Four sibling repos created, each with `Makefile`, `main.c`, `README.md`,
BSD-2 `LICENSE`: `~/axil-nd-core`, `~/axil-nd-other`, `~/axil-nd-level`
(+ `include/level/level.h`), `~/axil-nd-vanilla`. None of the four is a git
repo yet. All four build warning-free, load from the engine, and have their
behaviour asserted in `test.sh`.

Also changed after that: all four `Makefile`s no longer hardcode
`/home/quirinpa/axil-nd/nd-mod.mk`; they now resolve `$(PREFIX)/share/axil-nd/`
or fall back to a sibling `../axil-nd/` checkout (§7), so the next 15 repos
inherit a path that works outside one developer's home. Suite re-run green
after the change.

**Wave 1 started, 3 of 4 modules built** (none yet in `mods.load`, so none yet
asserted):

| module | state |
|---|---|
| `~/axil-nd-wts` | ported, builds clean; `xy_install` + 11 `HD_WTS` words, no events |
| `~/axil-nd-stone` | ported, builds clean; `on_spawn` (struct-by-value `struct bio`) + `on_add`, `map_where`/`object_add`, `XXH32` from `<xxhash.h>` |
| `~/axil-nd-biome` | ported, builds clean; 19 `HD_SKEL` + `HD_BIOME[16]`, no events |
| `~/axil-nd-shop` | **ported** — unblocked by the decorator chain (`~/axil-nd-core/core.h`), see §7 |

`wts`, `stone` and `biome` are built but still not in `mods.load`, so they load
nothing and are unasserted. `shop` IS loaded and asserted. **Wave 1: 4 of 4
ported, 1 of 4 in the suite.**

**The suite is intermittently failing on the persistence regression** — see
§12.1. It is not caused by anything in Phase 1 or Wave 1, but it currently
makes `./test.sh` an unreliable gate, so that is the first thing to settle.

---

## 10. Open questions

Three of the five questions this section used to carry are now answered, and
are recorded in §6. What remains:

1. **The persistence flake (§12.1) — settle this first.** `./test.sh` fails
   the two-boot regression in roughly 1 run of 3, so there is no trustworthy
   green until it does. The cheap discriminator is `save` + `SIGKILL` versus
   `save` + clean quit, which separates a lazy commit from a reopen-path bug.
2. **`on_icon` return composition (§7)** — last-wins confirmed, so the four
   decorator modules cannot each own `on_icon`. The old engine passed the
   running icon to each module in turn; XY's model has no equivalent, so one
   of them has to become the single owner or the engine has to grow a
   decorator chain. This blocks `shop` (Wave 1) and also `drink`, `plant` and
   `fight`.
3. **GitHub side** — push the new repos as `tty-pt/axil-nd-<name>`, or keep
   them local-only for now? They are not git repos yet, and none of the four
   has been committed.
4. **`nd-core`'s place** — standalone in the old superproject, and now ported
   and loading. The only open question is whether it stays in the slice or
   becomes a separate concern, since it is the odd one out (icon rendering, not
   a gameplay system).
5. **The 7 non-events** (`on_birth`, `on_death`, `on_murder`, `on_will_attack`,
   `on_mortal_life`, `on_mortal_survival`, `on_mortal_update`) — §6 and §7
   settled the mechanics: these are not engine events at all, so there is no
   firing site to re-wire and nothing to preserve. They ship as commented-out
   source. The remaining question is whether creating them is ever wanted, and
   if so that `on_mortal_*` is a *group* decision, not three independent ones.
   Affects `mortal`, `fight`, `seat`, `spell`. A design question, not a port
   question.
6. **The `eng_nd_flush` sweep** — §6 fixed `do_status` and flagged the class,
   but the other `do_*` handlers have not been swept and nothing tests for it.
   Worth settling before Wave 1, because `shop` writes from commands.

Closed in this revision:

- ~~`on_status` has no firing site~~ — **false.** Fired by `status`
  (`do_status`, `src/entity.c:236`). The census that suggested otherwise
  searched for the event name instead of the `nd_evt_*` wrapper name. All 20
  hooks are live; see §6.
- ~~Dead lifecycle hooks: add firing sites, or port as documented-dead?~~ —
  reframed above; there are no firing sites to add.
- ~~Live/dead audit of all 20 hooks~~ — **done**, §6. All 20 have exactly one
  real call site.

## 11. Key files

Engine tree:

| path | role |
|---|---|
| `Makefile` | include-order fix (§2.1), `mods` target + the `FORCE` fix (§6), `install-papi` |
| `include/papi/nd-xy.h` | module-facing service API: `nd_printf`, `nd_last` |
| `include/papi/nd-hooks.h` | canonical `XY_DECL` signatures for all 20 events |
| `include/papi/nd-xy-types.h` | module value types; includes `nd-hd.h`; `ACT_DROP = 8` |
| `include/papi/nd-hd.h` | `enum hd` + the tagged module-handle API (new in 0.2) |
| `include/papi/nd.h` | legacy `struct nd` vtable — deleted in Phase 3 |
| `include/uapi/type.h` | `sic_str_t` + the 20 `nd_evt_*` engine fire prototypes |
| `include/uapi/io.h` | `nd_hds[]`, `hd_resolve()`, `hd_mod_open()`; includes `nd-hd.h` |
| `include/uapi/object.h` | `struct icon` + `enum base_actions` (`ACT_DROP = 8`) |
| `src/nd_events.c` | canonical 20 `XY_DEF`s + the `nd_evt_*` wrappers (the firing sites) |
| `src/nd_xy.c` | io providers + path-aware `nd_mods_load()` |
| `src/nd_api.c` | service providers; the HD indirection of 0.2 |
| `src/entity.c` | `do_look_at`, `do_status` — the `eng_nd_flush` fix is here (§6) |
| `src/io.c` | `eng_nd_write`, the history+dedup filter whose semantics matter (§6) |
| `src/mods.c` | residual sic registry + dlopen loader — deleted in Phase 3 |
| `src/world.c` | `shared_init()`, module-table registry, `mod_load_all()` |
| `src/libaxil-nd.c` | single-TU module; folds in nd_xy/nd_events/nd_api |
| `nd-mod.mk` | the shared module build contract, installed to `$(PREFIX)/share/axil-nd/` |
| `mods/demo/demo.c` | the in-tree reference module — follow its shape |
| `mods.load` | the module list; tracked since 0.4, one line per module |
| `test.sh` | the suite: engine + modules, fixture, Phase 0.2/0.3/0.4 and Phase 1 assertions |

Slice module repos (all four: `Makefile`, `main.c`, `README.md`, `LICENSE`):

| path | role |
|---|---|
| `~/axil-nd-core/` | `on_icon`; struct return across the bus |
| `~/axil-nd-other/` | `on_add` + `HD_OBJ`/`HD_SKEL`; the `HD_*` indirection probe |
| `~/axil-nd-level/` | `level`/`level_up` cross-module API, own corm table, `on_status` |
| `~/axil-nd-level/include/level/level.h` | module-owned public header (`XY_DECL`) |
| `~/axil-nd-vanilla/` | `on_new_player`; the two non-existent events, kept commented |

References:

| path | role |
|---|---|
| `~/nd/ND_PORT.md` | original port record |
| `~/nd/module.mk` | the old build rule `nd-mod.mk` replaces |
| `~/nd/include/uapi/object.h:48` | the engine's own `ACT_DROP = 8` |
| `/tmp/nd-basics/`, `/tmp/nd-core/` | the cloned originals the slice was ported from |
| `~/site/AGENTS.md:34-41` | the XY module DAG contract to follow |

---

## 12. Findings about axil itself

Separated from the port because these are not module problems — they are
properties of axil/libxylem/corm that the port work exposed, and that outlive
this project. Each is recorded with what is actually measured, so the
difference between "observed" and "hypothesised" stays visible.

### 12.1 The store loses recently-saved data on an unclean shutdown — OBSERVED

`./test.sh`'s two-boot persistence regression fails intermittently, and the
failure is real rather than a test artifact. Measured over six consecutive
runs: **4 pass, 2 fail** (and 3 consecutive failures earlier in the session,
so it is frequent, not rare). The failing assertion is always the same:

```
FAIL: boot B re-created the player
```

which is `test.sh:762` — boot A created the player and `save`d it, boot B came
up on the same store, found no player, and ran `eng_object_add` again. The
store file is non-empty (`test.sh` asserts that) and boot A's log does contain
the creation, so the write reached the file system but the *player* was not
readable on the next boot.

**Mechanism, partially established.** Boot A is driven as `connect` → `save` →
`sleep 0.3` → `kill -SEGV`, and boot A is killed with `SIGSEGV` rather than
exiting cleanly. So the loss is somewhere in "saved, then died without a
clean shutdown, then reopened". Two candidates that the current test cannot
distinguish:

- the store commits lazily and `SIGSEGV` bypasses whatever would have flushed
  it, so the 0.3 s sleep is a race against writeback rather than a guarantee;
- the write landed but the reopen path misses it (an index or a header that is
  written on a clean exit only).

**This is a robustness property, not just a test problem.** A player who saves
and then crashes — a segfault, a `kill -9`, an OOM kill, a power cut — can lose
the save. Until this is understood, "saved" does not mean "durable", and the
0.3 s sleep in the suite is load-bearing in a way nobody intended: it is
masking a race, not providing a guarantee. Distinguishing the two candidates
is cheap — a `save` immediately followed by `SIGKILL` versus one followed by a
clean quit will separate lazy commit from a reopen-path bug — and it is the
first thing to run, because until it is settled the suite is a flaky gate on
the whole port.

### 12.2 `xy.last()` is unusable mid-dispatch, and leaks across a nested one

Already covered in §7 because it is what blocks nd's icon chain, but it is a
libxylem bug in its own right and not an nd one:

- `xy_last_ran` is cleared before the module loop and only set to the total
  afterwards (`dispatch.c:270` / `:340`), so `xy.last()` from inside a handler
  always returns `XY_ERR_NOTFOUND` and copies nothing. The shared return buffer
  is correct; only the guard on reading it is wrong.
- Worse, a handler that triggers a *nested* dispatch leaves `xy_last_ran` set on
  the way out, so a later handler in the outer dispatch can read the **nested**
  hook's result and take it for its predecessor's. The guard is not merely
  useless mid-dispatch, it is actively misleading.

The fix is to update `xy_last_ran` per module and scope it to the hook, but the
nested case has to be handled in the same change or the fix introduces a worse
bug. Until then, `sic_last()`-style composition has no working XY equivalent.

### 12.3 A test gate that fails a third of the time trains people to ignore it

Follows from 12.1, and is why it is worth fixing even though it is only a test.
`./test.sh` is the single gate for this whole project, and a gate that is red
~1 run in 3 with no signal about which change caused it is worse than no gate:
the cheapest human response is to re-run until green, which is exactly how a
real intermittent regression would survive review. A retry-on-known-flake is
the wrong fix (it hides a real bug); the right fix is 12.1.

There is a second, quieter instance of the same mistake in the suite itself,
found while building the icon chain: the room-glyph assertion read a `WARN` that
`nd-core` printed **inside** its own `on_icon`, *before* the decorator loop, so
it could not see the decorators' effect and passed while a decorator replaced
every room's glyph. A marker emitted by the producer, before composition, is not
a canary for composition. The marker now prints after the chain, and the suite
was verified to fail when a decorator is made to clobber (see §7).

### 12.4 Minor: the suite's engine process is tracked under a misleading name

`test.sh:118` stores the main engine's PID in `mux_pid` and the trap kills
`${mux_pid}`. There is no mux; the variable is simply misnamed, which makes the
trap hard to audit — the process that must not survive a run is the one whose
name says it is a relay. Worth renaming to `axil_pid` when this file is next
touched. The trap does kill the engine, but there is a shutdown race: a `pgrep`
taken the instant a successful run finishes can still catch the process, which
then exits on its own within a second or so. So "no stray after a clean run"
holds a moment later, not at the moment the trap returns — measured, after
`./test.sh` printed `axil-nd ok` the engine was still listed and gone by the next
command. The random port (`test.sh:23`) means such a window is unlikely to be
reused by the next run, but it is not zero, and it is why a hard-killed run's
leak can present as an intermittent test failure rather than as what it is.

The practical consequence: to tell whether a previous run left something
behind, check `pgrep axil` and wait — not whether the port still answers.
