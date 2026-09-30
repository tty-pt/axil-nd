/* Tailwind v3 configuration for the NeverDark browser client.
 *
 * `content` is the exact list `src/app.css` used to declare inline with v4's
 * `@source`. Nothing else is scanned on purpose: left on automatic detection,
 * Tailwind v3 walks the whole tree and treats anything it finds as a class
 * list -- the markdown, the man pages, the C sources, the build output under
 * cjs/ and esm/. `table`, `container`, `collapse` and `truncate` all reached
 * the production bundle from `src/world.c` and `man/get.10` that way, and
 * this repo's own prose leaked `opacity-50` and `flex-wrap`. tools/verify-css.mjs
 * fails the build if any of that comes back.
 *
 * Paths are relative to the CWD, and the build runs the CLI from the repo
 * root. `styles.css` and `src/app.css` are deliberately not listed: v4 scanned
 * neither (it used `source(none)` plus the two `@source` lines above).
 *
 * No `darkMode`: the light/dark behaviour is `color-scheme: light dark` in the
 * plain `:root` block, not a Tailwind dark variant, and no `dark:` class is
 * used anywhere in the client.
 */
module.exports = {
	content: ['./index.html', './index.js'],
	corePlugins: { preflight: false },
};
