# IDEAS — a flexible presentation layer for NeverDark

> Brainstorm / high-level plan. No code committed against this yet. This file
> captures the direction so future work has a shared north star; it is not a
> spec and every item here is negotiable.

## North star

NeverDark's presentation should be a **pluggable, data-driven layer**, so that
operators/users can customize how the game looks and works **in their part of
the universe** — one region might keep the terminal + panels, another might
render a 3D scene, another a custom HUD.

`bud` is the default **HTML interpreter** in that layer, not the frame of the
whole thing. The terminal stays xterm.js; the map stays whatever its interpreter
makes it.

## Guiding pattern (already used by the site)

The site's own rule: **framework-neutral data + a component-framework pair that
interprets it.** hyle carries UI hints opaquely; bud/React interpret; SSR markup
is the contract. The flexible-game design is the same idea one level up:

```
engine + region modules
        │  produce
        ▼
neutral presentation document   (no DOM concepts)
        │  consumed by an interpreter chosen per surface/region
        ├── html      (bud, default; SSR + WASM)
        ├── terminal  (xterm.js; ANSI/text)
        ├── map       (server text today)
        └── scene3d   (future WebGL/canvas; not bud's job)
```

Bud can host the `<canvas>` node and own the surrounding HUD/panels, but a 3D
region needs a **sibling interpreter**. Framing bud as *one* interpreter — not
*the* renderer — is what keeps the 3D goal reachable.

## Current state and the gap

Today's presentation extension surface is ad-hoc and non-structural:

- `vtf` — per-tile glyph + colour + flags only
  (`src/mods.c:86`, `eng_vtf_register`; `src/view.c:56-104`).
- `on_view_flags` / `on_icon` / `on_examine` — per-tile semantics/icons
  (`include/nd/hooks.h:34-40`).
- `art` — a background image path per object (`object_art`).
- Module-registered **BCP idens** — new binary frame kinds the fixed JS client
  happens to know (`bcp` table; hp/mp/stats/equipment from
  `external/nd-mortal`, `nd-spell`, `nd-attr`, `nd-equip`).
- `eng_action_register` — action label + icon only.

None of these lets a region define **layout or widgets**. Presentation is also
split arbitrarily: the server renders the map as ANSI text
(`src/view.c:441-443` → `fbcp_view_buf`), while `index.js` renders everything
else. "Flexibility" today means "add another frame id and another JS custom
element".

## Targets

The concrete things agreed on so far. Listed roughly in dependency order.

### T1 — Replace the web components with bud (keep xterm)

The custom elements in `index.html` / `index.js` become a **C-isomorphic bud
renderer**:
`nd-bar`, `nd-contents`, `nd-item`, `nd-content-actions`, `nd-map`,
`nd-look-at`, `nd-avatar`, `nd-equipment`, `nd-stat`, `nd-button`,
`nd-notarget`, and the `stats`/`equipment` modals.

- Keep **xterm.js** for the terminal; it is not replaced.
- Move the bulk of the client logic from JS to C: the `sub` state
  (`index.js:43-127`), BCP frame decoding (`index.js:373-566`), and the ANSI→HTML
  `tty_proc` converter (`index.js:199-334`).
- One `nd_ui_render(state)` shared by native SSR and WASM (the
  `docs/C-ISOMORPHIC-BUD.md` pattern).

### T2 — Add a generic duplex-stream bridge to `libbud`

The client needs push (a WebSocket), which bud has none of today — only
one-shot `bud_host_fetch` (`external/libbud/src/bud_wasm_app.c:34-41`) and the
6-symbol import allowlist (`scripts/wasm-allowed-imports.lst`).

Add neutral, domain-free primitives:

- host imports: `bud_host_stream_open(url,len,id)`,
  `bud_host_stream_send(id,ptr,len)`, `bud_host_stream_close(id)`, and a generic
  named output channel `bud_host_output(name,len,ptr,len)`;
- a WASM export `bud_stream_callback(id, kind, data, len)` (kind =
  open/message/close/error), mirroring the `wasm_fetch_callback` pattern;
- a generic JS implementation (`WebSocket`, output-sink registry) that talks
  only about "streams" and "outputs" — no game/domain identifiers
  (AGENTS.md §6).

Native builds keep no-op stubs, exactly like the fetch path today.

### T3 — Host the renderer in axil-nd (libbud + WASM target)

- Add `-lbud` and `#include <bud/bud.h>` to `external/axil-nd`; libbud installs
  to `/usr/include/bud` + `/usr/lib/libbud.so` and is already in the site's
  `install-libs` set.
- New dual-compiled unit, e.g. `src/ux/nd_ui.c` (+ `nd_ui_state.h`): state
  struct, `nd_ui_render()`, `wasm_init()`, `bud_app_render()`, and the BCP
  decoder. Pure bud + pure C only (no `axil_`/`corm_`/`XY_` — same UX-purity
  rule as the site's `mods/*/ux`).
- **SSR shell:** `handle_nd` (`src/libaxil-nd.c:505-525`) renders a small HTML
  shell containing `#bud-root`, `#bud-state`, and the xterm host via bud,
  instead of serving the static `index.html`.
- **WASM build:** axil-nd uses `mk` (`include.mk`), not the site's `build.mk`,
  so add a Makefile rule compiling `nd_ui.c` + `libbud.c` + `bud_wasm_app.c`
  with `clang --target=wasm32-wasi -mexec-model=reactor -Wl,--export-all
  -Wl,--allow-undefined` → `htdocs/nd.wasm`, deployed by the npm build.
- The engine's BCP protocol is **unchanged**; all socket bytes are forwarded to
  WASM, which splits `#b` frames (into state) from terminal bytes (out through
  `bud_host_output("term", …)` → xterm).

### T4 — Define the neutral presentation document

Generalize the ad-hoc hooks (`vtf`, `bcp`, actions, icons, art) into a
**view registry** so region modules contribute **widgets**, not bespoke frames.

- A neutral, DOM-free document/tree of surfaces with state and a `kind` per
  node (e.g. `terminal`, `text`, `list`, `grid`, `bar`, `image`, `slot`,
  `canvas`, and later `scene3d`).
- The document is **composed server-side** (engine + region modules author it),
  which is what makes per-region customization real.
- hyle-style separation: the document is neutral; interpreters know nothing
  about storage.

### T5 — Interpreter registry

`kind → renderer`, so the client is a set of interpreters rather than fixed
components:

- `html` (bud) is the default; `terminal` (xterm) and `map` prove the seams;
- a second non-bud interpreter (canvas/2.5D) is the *proof* that bud is not
  privileged, before any 3D work.

### T6 — Per-region customization

A region/module can select/contribute presentation for its surfaces. This is
where the "one region is 3D / custom HUD" goal lands. Depends on T4 + T5 and on
the A-vs-B decision below.

## Design axes (decide these, and the rest follows)

1. **Layout locus** — client-fixed (today) → server/region-composed spec →
   fully server-authored tree + patches.
2. **Contract** — typed declarative widget spec vs. raw bud tree/patch ops vs.
   arbitrary code.
3. **Interpreter registry** — `kind → renderer` (T5).
4. **Custom interpreter source** — build-time bundles selected by data, runtime
   `.wasm` plugins, or declarative-only.
5. **Trust** — a user's custom rendering is seen by *other* players in their
   region. This is the crux.

## The one tension to resolve first

If regions can go 3D, either:

- **(A) Declarative-only** — a rich fixed vocabulary of widget/scene kinds;
  regions compose specs, no custom code. Safe, composable, SSR-able; ceiling is
  the vocabulary we design.
- **(B) Code plugins** — regions ship `.wasm`/JS interpreters loaded at runtime.
  Unbounded, but a sandbox/moderation problem the moment other players render it.

Everything downstream (bud default, transport, registry) is compatible with
both; A vs B decides whether the contract is a **spec** or a **plugin ABI**.

## Phases

- **P0 — establish the neutral contract.** T1–T3: one presentation state struct,
  bud as the `html` interpreter, xterm kept, generic stream bridge. Demonstrates
  the wiring and deletes the hand-written component JS.
- **P1 — make it data-driven.** T4: view registry; region modules contribute
  widgets declaratively instead of new BCP frames.
- **P2 — interpreter registry.** T5: add a second interpreter to prove the
  abstraction, then a `scene3d` kind.
- **P3 — region bundles + trust.** T6: per-region interpreter selection and (if
  B wins) a sandbox.

## Open questions

1. For 3D: **declarative scene widgets (A)** or **user-supplied code (B)**?
   (Decides spec vs plugin ABI.)
2. Presentation document **composed server-side** (modules author it) or
   client-side (fixed client pulls state)? Server-side is what makes region
   customization real.
3. Is the terminal/ANSI view a **fixed primitive** or just another interpreter
   of the same document? (The map is already server text, so treating both as
   interpreters is the clean generalization.)
4. Are regions **trusted operators** or **arbitrary users**? (Sandbox shape.)
5. Does the site's framework-pair rule extend here — is a future **non-bud**
   interpreter required to implement the same contract? If yes, define the
   neutral document before wiring bud in.

## Risks / things to validate early

- **`mk` vs `build.mk`** — axil-nd needs its own WASM rule (WASI_CC probe + `.d`
  dependency tracking).
- **Tailwind scanning** — bud emits literal classes; `tools/verify-css.mjs`
  scans only `index.html`/`index.js`, so classes emitted from C must be
  literal/safelisted (cf. the `BUTTON_CLASS` workaround).
- **Id alignment** — the SSR shell and the WASM tree must match at mount
  (`docs/C-ISOMORPHIC-BUD.md` §3).
- **CSP / `bud-state` hash** — replicate the site's inline-state accommodation
  if a strict CSP is added.
- **Dynamic BCP idens** are module-registration-ordered; the C decoder must
  resolve them the same way `index.js` does today.
- **axil `ws.c` frame caveat** — a frame's payload must arrive in a single
  `lower_read` (`ND_PORT.md`, Known quirks).
- **Non-Chroot / No-JS** — a live MUCK can't be played without JS; the SSR shell
  must still render meaningfully.

## Non-goals (this direction)

- Replacing xterm.js.
- Changing the engine's BCP wire protocol (transport is preserved; only the
  consumer moves).
- Committing to 3D now — T6/P2/P3 are the runway, not the near-term work.
