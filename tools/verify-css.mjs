#!/usr/bin/env node
/*
 * Build guard for the NeverDark client stylesheet.
 *
 * Every bug this repo had with CSS was a *silently missing* rule: the build
 * exited 0, `make test` was green, and the page came out unstyled. `test.sh`
 * only ever checked for `.flex` and `#right-panel`, both of which are present
 * on a completely broken bundle. This is the check that would have caught them.
 *
 * Run it after `npm run build`:
 *
 *     node tools/verify-css.mjs
 *
 * It is deliberately not wired into package.json -- that file is the user's
 * staged work. Three directions:
 *
 *   - fails if a class in the bundle does not appear in index.html, index.js,
 *     src/app.css or styles.css. Tailwind v4 has no @source here by default and
 *     scans the whole tree, including the markdown, so a class named in a
 *     *comment* in this repo's docs used to end up in the production CSS.
 *   - fails if a class the client needs at runtime is absent from the bundle,
 *     which is the direction the old suite could not see at all.
 *   - the forward check (5): every class the markup or the JS can put on an
 *     element must resolve to a rule -- this is what would have caught the
 *     missing HP-bar fill (`.c1` arrives through the `x` attribute) and the
 *     dead selected-item highlight (`.ctb` assigned through a variable).
 */

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const read = (p) => {
  try {
    return readFileSync(join(root, p), "utf8");
  } catch {
    return "";
  }
};

const bundle = read("htdocs/app.css");
if (!bundle) {
  console.error("verify-css: htdocs/app.css not found -- run `npm run build` first");
  process.exit(2);
}

const sources = ["index.html", "index.js", "src/app.css", "styles.css"]
  .map(read)
  .join("\n");

/* Class selectors in the bundle. Escaped identifiers (`gap-x-\[8px\]`) are
 * unescaped, and anything that is not a plain class (an id, a pseudo-class, a
 * custom property) is dropped.
 *
 * `url(...)` contents are blanked first. A path like `mineral/gold/1.jpeg`
 * contains dots that are indistinguishable from class selectors to a regex,
 * and a `@font-face` src is worse still: its `.ttf` was counted as a class
 * named "ttf". */
const noUrls = bundle.replace(/url\([^)]*\)/g, "url()");
const inBundle = new Set();
for (const m of noUrls.matchAll(/\.(-?[_a-zA-Z][\w-]*)/g)) inBundle.add(m[1]);

const failures = [];

/* 1. Nothing may reach the bundle by accident. */
const leaked = [...inBundle].filter((c) => !sources.includes(c));
if (leaked.length)
  failures.push(
    `${leaked.length} class(es) in htdocs/app.css are not in index.html, index.js, ` +
      `src/app.css or styles.css:\n  ${leaked.sort().join("\n  ")}\n` +
      "  -> a comment or doc is being scanned, or a class was renamed without rebuilding"
  );

/* 2. Everything the client needs must be there. The palette first: 38 custom
 * properties that styles.css consumes and never defines. */
const required = [
  // :root palette, consumed by styles.css .cf0-.cf15 / .c0-.c15
  "--cb:", "--cf:", "--cc:", "--cd0:", "--cd15:", "--cdb:", "--cdf:", "--cdc:",
  "--c0:", "--c1:", "--c15:",
  // the button rules, which a custom element cannot get from utilities
  ".btn", "nd-button.btn",
  // the explicit size classes, which replaced an interpolated bracket class
  ".pad-8", ".txt-17", ".box-24",
  // geometry migrated out of the atomic vocabulary
  ".h-full", ".absolute", ".inset-0", ".opacity-50", ".flex-wrap",
  ".flex-col", ".items-center", ".relative", ".box-border", ".rounded-full",
  ".gap-x-\\[8px\\]", ".gap-y-\\[8px\\]", ".mr-\\[8px\\]",
  // game sheet still linked
  "#main", "#holder", "#right-panel", "#cb", ".terminal",
  // the page frame (basics.css:64-75) and its dark half (basics.css:310-314)
  "min-height:100%", "caret-color:", "padding:8px",
];

const missing = required.filter((token) => !bundle.includes(token));
if (missing.length)
  failures.push(
    `${missing.length} required rule(s) absent from htdocs/app.css:\n  ${missing.join("\n  ")}`
  );

/* 3. The atomic vocabulary must stay dead. These came from the site shell's
 * stylesheets, which this port does not ship; if one reappears in the markup
 * it is dangling again, exactly as before. */
const atoms = [
  "btn", "abs", "a", "tac", "tr5", "svf", "rel", "v0", "fic", "f", "h8", "fw",
  "bb", "mr8", "p8", "ts17", "th17", "round", "shf", "sf", "p", "p16", "c",
  "fcc", "oh", "dn", "ts9", "tm", "pxs",
];
const markup = read("index.html") + read("index.js");
const revived = atoms.filter((a) => new RegExp(`class="[^"]*\\b${a}\\b`).test(markup));
if (revived.length)
  failures.push(
    `atomic class(es) referenced by the markup but defined nowhere: ${revived.join(", ")}\n` +
      "  -> the site-shell vocabulary is coming back; use the Tailwind utility instead"
  );

/* 5. Forward check: every class the markup or the JS can put on an element
 * must resolve to a rule. The reverse check (1) cannot see this direction,
 * which is how `.c1` (the HP bar fill) and `.ctb` (the selected room item)
 * shipped as dead names: the bar's colour arrives through the `x` attribute,
 * not a class literal, and `ctb` is assigned through a variable.
 *
 * A selector matches in either spelling: the minified bundle writes
 * `.gap-x-\[8px\]`, so every non-word character in the class may carry an
 * optional backslash. `.c1` must not match `.c10`, so the name is fenced. */
const selRe = (c) =>
  new RegExp(
    "\\." + [...c].map((ch) => (/[\w-]/.test(ch) ? ch : `\\\\?\\${ch}`)).join("") +
      "(?![-\\w])"
  );

const used = new Set();
// 5a. class="..." literals in the markup and in the JS-emitted templates
//     (template holes like ${cls} are not classes and are dropped)
for (const src of [read("index.html"), read("index.js")])
  for (const m of src.matchAll(/class="([^"]*)"/g))
    for (const c of m[1].split(/\s+/))
      if (c && !/[{}$]/.test(c)) used.add(c);
// 5b. className += "..." literals
for (const m of read("index.js").matchAll(/className\s*\+?=\s*["']([^"']*)["']/g))
  for (const c of m[1].split(/\s+/)) if (c) used.add(c);
// 5c. classList.add/remove/toggle("...")
for (const m of markup.matchAll(/classList\.(?:add|remove|toggle)\(["']([^"']*)["']\)/g))
  used.add(m[1]);
// 5d. x="..." on the bars: Bar.render puts x straight into the class list
for (const m of read("index.html").matchAll(/\sx="([^"]*)"/g)) used.add(m[1]);
// 5e. the two names no literal contains, with where they come from:
//     - "ctb": index.js:846 assigns it through the `background` variable
//     - cf0-cf15: setImageClass builds "cf" + (fg + 8) at runtime
used.add("ctb");
for (let n = 0; n < 16; n++) used.add(`cf${n}`);

/* `day` is an inert marker: #main is day by the *absence* of `night`, and the
 * original defined no `.day` rule either. Requiring it would be wrong. */
used.delete("day");

/* Dead in the original too (STYLE-AUDIT.md "Not port regressions"): the JS
 * emits them and no sheet on either host defines them. `popper` is inert --
 * the library positions with inline styles. `targeting` vs `targetted` is the
 * known three-way spelling mismatch (CSS.md Q9, still open). `tbbold` never
 * existed. If any of them ever gains a rule, delete it from this list. */
for (const dead of ["popper", "targeting", "targetting", "tbbold"])
  used.delete(dead);

const unresolved = [...used].filter((c) => !selRe(c).test(bundle));
if (unresolved.length)
  failures.push(
    `${unresolved.length} class(es) used by index.html/index.js resolve to no rule:\n  ${unresolved.sort().join("\n  ")}`
  );

// 5f. the one element selector the port owns: #directions is a <label>, and
// the game sheet would otherwise win align-items with `end`
if (!/\blabel\s*\{/.test(bundle))
  failures.push("the `label` rule is absent from htdocs/app.css:\n  label");

/* 4. The terminal theme must survive. xterm paints its background as an
 * inline style on a wrapper styles.css predates, and injects its own
 * foreground/palette sheet under `.xterm-dom-renderer-owner-N`; both beat
 * plain rules, so src/app.css answers with `!important`. A library bump that
 * drops or renames any of these would silently re-blacken the terminal or
 * revert it to the stock Tango palette, exactly the regression this section
 * exists for. brightBlack (slot 8) is the one slot the theme never declares
 * and is checked absent by design. */
const termMissing = [];
if (!bundle.includes(".xterm-scrollable-element"))
  termMissing.push(".xterm-scrollable-element (the xterm 6 background layer)");
if (!bundle.includes(".xterm-rows"))
  termMissing.push(".xterm-rows (the terminal default foreground)");
if (!bundle.includes(".xterm-cursor"))
  termMissing.push(".xterm-cursor (the terminal cursor colour)");
for (let n = 0; n < 16; n++) {
  if (n === 8) continue;
  for (const kind of ["fg", "bg"])
    if (!bundle.includes(`.xterm-${kind}-${n}`))
      termMissing.push(`.xterm-${kind}-${n}`);
}
if (termMissing.length)
  failures.push(
    `${termMissing.length} terminal rule(s) absent from htdocs/app.css:\n  ${termMissing.join("\n  ")}`
  );

if (failures.length) {
  console.error(`verify-css: ${failures.length} problem(s)\n`);
  for (const f of failures) console.error(f + "\n");
  process.exit(1);
}

console.log(
  `verify-css: ok -- ${inBundle.size} classes, palette and button rules present`
);
