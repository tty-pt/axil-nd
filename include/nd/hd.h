#ifndef ND_HD_H
#define ND_HD_H

/*
 * hd.h — the module-visible table handle namespace (MODS.md §0.2).
 *
 * A game module names engine tables with the `enum hd` below
 * (`nd_get(HD_OBJ, &obj, &ref)`) and names the tables IT opened with
 * `nd_open()` (nd-class's "class", nd-level's level_hd, nd-attr's attr_hd).
 * Those two sets used to be the same `unsigned`, which made every table
 * access ambiguous: `HD_OBJ` is the small integer 7, and so is a corm handle
 * that happens to be 7.
 *
 * So there are two namespaces and one `unsigned`:
 *
 *   0x00000000 .. HD_MAX-1   an `enum hd`, resolved engine-side in nd_hds[]
 *   ND_HD_MOD | idx           a table a module opened, idx into the engine's
 *                             module-table registry
 *
 * This header is included by BOTH the engine (`uapi/io.h`, which no longer
 * defines `enum hd` itself) and modules (`nd/xy-types.h`). It therefore
 * declares nothing but the enum and these inline helpers: no struct or enum
 * that the uapi headers also define, and no `extern` the engine TU would have
 * to define. `nd_hds[]` and the resolver live in `uapi/io.h`.
 */

/* The engine's built-in tables. Values are ABI: a module compiled against
 * this header passes these to nd_get/nd_put/nd_iter, and the engine indexes
 * nd_hds[] with them. Append only -- inserting renumbers every module. */
enum hd {
	HD_FD,
	HD_SKEL,
	HD_DROP,
	HD_ADROP,
	HD_BIOME,
	HD_WTS,
	HD_RWTS,
	HD_OBJ,
	HD_OBS,
	HD_CONTENTS,
	HD_TYPE,
	HD_RTYPE,
	HD_BCP,
	HD_ELEMENT,
	HD_HD,
	HD_MAX,
};

/* Tag bit for a module-opened table. */
#define ND_HD_MOD ((unsigned) 0x80000000)

/* Build / classify a module table handle. */
static inline unsigned nd_hd_mod(unsigned idx) { return ND_HD_MOD | idx; }
static inline int nd_hd_is_mod(unsigned hd) { return (hd & ND_HD_MOD) != 0; }
static inline unsigned nd_hd_mod_idx(unsigned hd) { return hd & ~ND_HD_MOD; }

#endif /* ND_HD_H */
