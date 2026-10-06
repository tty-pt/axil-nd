/* nd_events.c — engine XY_DEF side for the nd/hooks.h game events.
 *
 * Each XY_DEF emits the canonical adapter + a static inline dispatcher that
 * runs every listener in the current region. Game modules XY_IMPL the
 * listeners they want.
 * Folded into src/libaxil-nd.c (single-TU module): do NOT compile standalone
 * and do NOT include nd/hooks.h here (XY_DEF + XY_DECL clash).
 *
 * WHY THE nd_evt_* WRAPPERS
 * -------------------------
 * The engine's event firing sites live in ordinary engine TUs (object.c,
 * entity.c, spacetime.c, world.c, ...) which CANNOT include nd/hooks.h:
 * nd/xy-types.h redefines struct icon / enum color / sic_str_t that the
 * uapi headers already define, and the same symbols then appear twice. So the
 * engine calls exported wrappers instead, declared in uapi/type.h.
 *
 * Two reasons not to use XY_DECL at the call sites:
 *
 *  1. Size. XY_DECL emits a static xy_adapter_t per hook per TU. xy_adapter_t
 *     is 4200 bytes (4096 of it the `ret` scratch buffer), so the 20 events
 *     would add ~84 KB of .data to every engine TU that includes
 *     uapi/type.h — 1.2 MB of pure padding in the .so once eight TUs pull it
 *     in, relocated on every dlopen.
 *
 *  2. Region. xy_call() dispatches from the *thread-local current region*, not
 *     from the caller's identity. Every wrapper below lives in the one TU that
 *     holds libaxil-nd's injected module context (xy, from xy-mod.h), so
 *     events always fire from the engine's own region subtree. Game modules
 *     loaded by nd_mods_load() are children of exactly that region, which is
 *     what makes them see the events at all.
 */

#include "nd/xy-types.h"

XY_DEF(int, on_status, unsigned, player_ref);
XY_DEF(int, on_examine, unsigned, player_ref, unsigned, ref, unsigned, type);
XY_DEF(int, on_add, unsigned, ref, unsigned, type, uint64_t, v);
XY_DEF(unsigned short, on_view_flags, unsigned short, flags, unsigned, ref);
XY_DEF(struct icon, on_icon, unsigned, ref, unsigned, type, unsigned, player_ref);
XY_DEF(int, on_del, unsigned, ref, unsigned, type);
XY_DEF(int, on_clone, unsigned, orig_ref, unsigned, nu_ref);
XY_DEF(int, on_update, unsigned, ref, unsigned, type, double, dt);
XY_DEF(int, on_move, unsigned, ref);

XY_DEF(int, on_vim, unsigned, ref, sic_str_t, ss);
XY_DEF(struct bio, on_noise, struct bio, bio, uint32_t, he, uint32_t, w,
	uint32_t, tm, uint32_t, cl);
XY_DEF(sic_str_t, on_empty_tile, view_tile_t, t, unsigned, side, sic_str_t, ss);

XY_DEF(int, on_new_player, unsigned, player_ref);
XY_DEF(int, on_auth, unsigned, player_ref);
XY_DEF(int, on_before_leave, unsigned, ent_ref);
XY_DEF(int, on_leave, unsigned, player_ref, unsigned, loc_ref);
XY_DEF(int, on_enter, unsigned, player_ref, unsigned, loc_ref);
XY_DEF(int, on_after_enter, unsigned, player_ref);
XY_DEF(int, on_spawn, unsigned, player_ref, unsigned, loc_ref, struct bio, bio,
	uint64_t, v);
XY_DEF(int, on_get, unsigned, player_ref, unsigned, ref);

/* ======================================================================
 * Phase 3: anchored, region-scoped dispatch (ST.md §27)
 * ======================================================================
 *
 * Until now every wrapper below called the XY_DEF-generated inline, which is
 * a bare xy_call from the current region -- and xy_call reaches the caller's
 * whole SUBTREE (xy.h §4.4), so a module loaded into planet 1 fired for an
 * event anchored in planet 2. Phase 2 gave a planet a region and a persisted
 * module set; nothing yet made that set RUN.
 *
 * The fix is a coarse->fine walk down the region tree: from the root, dispatch
 * the current region's OWN modules (xy_call_self, so no child leaks in), then
 * descend into the child that prefix-covers the event's anchor. The anchor is
 * a Morton code, so "is this region my planet" is a prefix test and no
 * per-event bookkeeping is needed.
 *
 * Two decisions are load-bearing and look like tidiness if you don't know why:
 *
 *  - The walk STARTS AT THE ROOT, not at the deepest covering region. Starting
 *    at the deepest match would be the obvious reading of "exact region" and it
 *    would stop the root's own modules (mods.load: attr, class, fight, level,
 *    mortal, race, spell) from seeing every event -- which silently deletes
 *    Level lines from `status` rather than failing loudly.
 *
 *  - ONE scratch buffer for the whole walk, copied out only on XY_OK.
 *    xy_dispatch memsets the caller's retp and returns XY_ERR_NOTFOUND when no
 *    listener ran, so a per-level buffer would let a finer region that has no
 *    listener DESTROY the value a coarser region already produced. Copying out
 *    on success is what makes "the last region that actually ran" the winner,
 *    and it matches what a single root-wide xy_call already returned.
 *
 * Deny needs nothing here, deliberately: xy_dispatch evaluates denies over the
 * ANCESTOR chain of the region it dispatches in, so a cosmos-level xy_deny
 * refuses planet-wide implementations without the walk knowing it exists, and
 * a deny is never evaluated against the region's own modules.
 */

/* Engine map/object access. Declared here rather than included: uapi/map.h
 * drags in uapi/st.h, and nd/xy-types.h -- which this TU needs, and which
 * nd/hooks.h would clash with -- already defines pos_t and morton_t. Same
 * pattern as libaxil-nd.c's own extern block. */
extern unsigned obj_hd;
extern int eng_map_has(unsigned thing);
extern void eng_map_where(pos_t p, unsigned thing);
morton_t pos_morton(pos_t p);

/* Does the region (id, plen) prefix-cover `code`? A canonical region id has
 * exactly its high plen bits set (xy_claim_at rejects anything else), so
 * comparing the top plen bits of each answers containment. plen 0 is the root
 * and covers everything; the shift is written as a divide-then-shift so plen
 * 64 cannot shift by 64. */
static inline int
nd_region_covers(uint64_t code, uint64_t id, uint8_t plen)
{
	if (plen == 0)
		return 1;
	if (plen > 64)
		return 0;
	return (code >> (64 - plen)) == (id >> (64 - plen));
}

/* Anchor code for a room ref. eng_map_has() FIRST: eng_map_where() MEMSETS an
 * unmapped room's pos_t, so asking it about an unknown room yields (0,0,0,0) --
 * world 0 -- and would file a positionless object under planet 0's region. That
 * is a cross-planet leak wearing the costume of a success, so "no anchor" is
 * reported instead and the caller falls back to a global dispatch. */
static int
nd_anchor_room(unsigned loc_ref, uint64_t *code)
{
	pos_t pos;

	if (loc_ref == NOTHING || !eng_map_has(loc_ref))
		return 0;
	eng_map_where(pos, loc_ref);
	*code = pos_morton(pos);
	return 1;
}

/* Anchor code for an object: the room it is IN. Used for the player_ref- and
 * ref-anchored events, including on_before_leave and on_move, both of which
 * fire before the move is applied and so see the old location.
 *
 * Existence is tested with corm_get() FIRST: on_del fires AFTER eng_object_move
 * has already corm_del'd the row, and corm_get_copy() aborts on a miss. The
 * pre-Phase-3 generated inline never read the object at all, so this is a new
 * way to crash that only the anchored wrapper can hit. A deleted object has
 * no location to anchor on, so "no anchor" (global dispatch) is the honest
 * answer -- the delete still reaches every region's on_del. */
static int
nd_anchor_object(unsigned ref, uint64_t *code)
{
	OBJ obj;

	if (ref == NOTHING)
		return 0;
	if (corm_get(obj_hd, &ref) == NULL)
		return 0;
	memset(&obj, 0, sizeof(obj));
	corm_get_copy(obj_hd, &ref, &obj);
	return nd_anchor_room(obj.location, code);
}

struct nd_scope {
	xy_adapter_t *adapter;
	void *args;
	void *ret;
	uint64_t code;
	unsigned ran;
	unsigned char scratch[XY_MAX_RET_SIZE];
};

/* The xy_region_each callback: keep the child that covers the anchor. Both
 * halves of the identity are kept because ids repeat down the left spine --
 * (0,16) and (0,17) share an id -- so an id-only match would descend into the
 * wrong region. Returning non-OK stops the enumeration, which is how xy.h
 * documents breaking out. */
struct nd_descend {
	uint64_t code;
	uint64_t child_id;
	uint8_t child_plen;
	int found;
};

static int
nd_scope_child(uint64_t child_id, uint8_t child_plen, void *ud)
{
	struct nd_descend *d = ud;

	if (nd_region_covers(d->code, child_id, child_plen)) {
		d->child_id = child_id;
		d->child_plen = child_plen;
		d->found = 1;
		return XY_ERR_NOTFOUND; /* stop: one child covers a given prefix */
	}
	return XY_OK;
}

/* One chain member: its OWN modules, then whichever child is still ours.
 * Coarse->finest falls out of the recursion -- the root's step runs first and
 * each step descends afterwards -- so there is nothing to sort. */
static int
nd_scope_step(void *ud)
{
	struct nd_scope *sc = ud;
	struct nd_descend d;

	d.code = sc->code;
	d.child_id = XY_REGION_INVALID;
	d.child_plen = 0;
	d.found = 0;

	if (xy_call_self(sc->scratch, sc->adapter, sc->args) == XY_OK) {
		memcpy(sc->ret, sc->scratch, sc->adapter->ret_size);
		sc->ran++;
		WARN("nd_scope: %s region id=0x%016llx plen=%u ran=%u\n",
			sc->adapter->name,
			(unsigned long long)xy_current_region(),
			xy_current_region_plen(), sc->ran);
	}

	xy_region_each(nd_scope_child, &d);
	if (d.found)
		xy_with_region(d.child_id, d.child_plen, nd_scope_step, sc);
	return XY_OK;
}

/* have_code 0 means the event is DECLARED GLOBAL (§27.1) -- on_noise and
 * on_empty_tile carry no position in their signatures at all -- and dispatches
 * from the root exactly as it did before Phase 3, reaching every region. */
static void
nd_scope_dispatch(void *retp, xy_adapter_t *adapter, void *args,
	int have_code, uint64_t code)
{
	struct nd_scope sc;

	if (!have_code) {
		xy_call(retp, adapter, args);
		return;
	}

	sc.adapter = adapter;
	sc.args = args;
	sc.ret = retp;
	sc.code = code;
	sc.ran = 0;

	/* "Nothing ran" is a zeroed ret, which is also what xy_dispatch returns
	 * when no listener ran: an anchor that reaches no module must look
	 * exactly like the pre-Phase-3 case, or a caller reading the return
	 * value would start seeing a difference that no module caused. */
	memset(retp, 0, adapter->ret_size);

	xy_with_region(XY_REGION_ROOT, 0, nd_scope_step, &sc);
}

/* --- engine-facing fire functions (uapi/type.h) -------------------------- */
/* One per event above. Signatures must stay ABI-identical to the XY_DEFs and
 * to the uapi/type.h prototypes.
 *
 * Each one now states its OWN anchor (§7.4.1) and hands it to
 * nd_scope_dispatch() instead of calling the XY_DEF-generated inline. That is
 * deliberately explicit rather than routed through a hook-name -> anchor
 * table: on_move and on_update fire per object per tick, so a strcmp over a
 * 20-entry table at every fire is a cost paid on the hottest path in the
 * engine, and a table would also hide the anchor choice one indirection away
 * from the event whose behaviour it decides. The generated inlines are
 * `static inline UNUSED`, so they simply go unused.
 *
 * The anchor is re-read per fire, not cached: an object's location changes, so
 * a cached code would go stale exactly when it mattered. */

int nd_evt_status(unsigned player_ref)
{
	struct on_status_args args = { .player_ref = player_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(player_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_status_adapter, &args, have, code);
	return ret;
}

int nd_evt_examine(unsigned player_ref, unsigned ref, unsigned type)
{
	struct on_examine_args args = { .player_ref = player_ref, .ref = ref,
		.type = type };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(player_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_examine_adapter, &args, have, code);
	return ret;
}

int nd_evt_add(unsigned ref, unsigned type, uint64_t v)
{
	/* No player_ref: anchor on the object's own location, and if it has none
	 * yet (a brand-new object is added with where=NOTHING) fall back to a
	 * global dispatch rather than guessing world 0. */
	struct on_add_args args = { .ref = ref, .type = type, .v = v };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_add_adapter, &args, have, code);
	return ret;
}

unsigned short nd_evt_view_flags(unsigned short flags, unsigned ref)
{
	struct on_view_flags_args args = { .flags = flags, .ref = ref };
	uint64_t code = 0;
	unsigned short ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_view_flags_adapter, &args, have, code);
	return ret;
}

struct icon nd_evt_icon(unsigned ref, unsigned type, unsigned player_ref)
{
	/* The VIEWER anchors it, not the drawn object: per-player icon
	 * dispatch exists so one player's rendering choice cannot leak into
	 * another's (SESSION-9m). */
	struct on_icon_args args = { .ref = ref, .type = type,
		.player_ref = player_ref };
	uint64_t code = 0;
	struct icon ret;

	int have = nd_anchor_object(player_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_icon_adapter, &args, have, code);
	return ret;
}

int nd_evt_del(unsigned ref, unsigned type)
{
	struct on_del_args args = { .ref = ref, .type = type };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_del_adapter, &args, have, code);
	return ret;
}

int nd_evt_clone(unsigned orig_ref, unsigned nu_ref)
{
	struct on_clone_args args = { .orig_ref = orig_ref, .nu_ref = nu_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(orig_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_clone_adapter, &args, have, code);
	return ret;
}

int nd_evt_update(unsigned ref, unsigned type, double dt)
{
	struct on_update_args args = { .ref = ref, .type = type, .dt = dt };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_update_adapter, &args, have, code);
	return ret;
}

int nd_evt_move(unsigned ref)
{
	/* Fires BEFORE the move is applied (object.c), so location is still the
	 * room being left: a planet's on_move must run in the planet the mover is
	 * leaving, not the one it is entering. */
	struct on_move_args args = { .ref = ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_move_adapter, &args, have, code);
	return ret;
}

int nd_evt_vim(unsigned ref, sic_str_t ss)
{
	struct on_vim_args args = { .ref = ref, .ss = ss };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_vim_adapter, &args, have, code);
	return ret;
}

/* --- declared global (§27.1): no position in the signature at all --------- */

struct bio nd_evt_noise(struct bio bio, uint32_t he, uint32_t w, uint32_t tm,
	uint32_t cl)
{ return on_noise(bio, he, w, tm, cl); }

sic_str_t nd_evt_empty_tile(view_tile_t t, unsigned side, sic_str_t ss)
{ return on_empty_tile(t, side, ss); }

int nd_evt_new_player(unsigned player_ref)
{ return on_new_player(player_ref); }

int nd_evt_auth(unsigned player_ref)
{
	/* Auth happens before the player has a room (world.c: nd_player_login),
	 * so there is no location to anchor on and this dispatches globally. */
	return on_auth(player_ref);
}

int nd_evt_before_leave(unsigned ent_ref)
{
	/* An entity leaving, not an object: read the location off the OBJ row.
	 * Fires before the move, so this is the room being left. */
	struct on_before_leave_args args = { .ent_ref = ent_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(ent_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_before_leave_adapter, &args, have, code);
	return ret;
}

int nd_evt_leave(unsigned player_ref, unsigned loc_ref)
{
	/* The ROOM argument, not the player's location: object_move fires leave
	 * and enter around the move (object.c), so location is already the new
	 * room by now and anchoring on it would fire `leave` in the room being
	 * entered. */
	struct on_leave_args args = { .player_ref = player_ref,
		.loc_ref = loc_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_room(loc_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_leave_adapter, &args, have, code);
	return ret;
}

int nd_evt_enter(unsigned player_ref, unsigned loc_ref)
{
	struct on_enter_args args = { .player_ref = player_ref,
		.loc_ref = loc_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_room(loc_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_enter_adapter, &args, have, code);
	return ret;
}

int nd_evt_after_enter(unsigned player_ref)
{
	struct on_after_enter_args args = { .player_ref = player_ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(player_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_after_enter_adapter, &args, have, code);
	return ret;
}

int nd_evt_spawn(unsigned player_ref, unsigned loc_ref, struct bio bio,
	uint64_t v)
{
	/* The room argument, like enter/leave: st_room_at fires this for the room
	 * it just carved, and the player's location is not yet it. */
	struct on_spawn_args args = { .player_ref = player_ref,
		.loc_ref = loc_ref, .bio = bio, .v = v };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_room(loc_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_spawn_adapter, &args, have, code);
	return ret;
}

int nd_evt_get(unsigned player_ref, unsigned ref)
{
	struct on_get_args args = { .player_ref = player_ref, .ref = ref };
	uint64_t code = 0;
	int ret;

	int have = nd_anchor_object(player_ref, &code);

	nd_scope_dispatch(&ret, &__xy_decl_on_get_adapter, &args, have, code);
	return ret;
}

/* Fire the arrival event for a player who has just been placed in a room.
 *
 * Only on_enter. on_new_player is NOT fired here: it belongs to first
 * creation and fires exactly once, from nd_player_login() in world.c, for
 * every code path (WS and raw) alike. This used to fire both, which is why
 * the engine kept a call_on_new_player() of its own in world.c and why demo
 * saw a second on_enter — the pair has been split up now that the engine's own
 * sites reach XY.
 *
 * A brand-new player is created with where=NOTHING (world.c:776), so
 * object_add()'s own on_enter never runs for them: landing a player is not the
 * same event as moving an object into a room, and this is the site that says
 * so.
 *
 * Routed through nd_evt_enter() rather than the generated inline so this site
 * is anchored like every other on_enter. Calling the inline here would have
 * been the one unscoped entry point into the planet-scoped event set, and it
 * would have leaked exactly once per player login -- the case a reviewer is
 * least likely to notice. */
void
nd_event_announce(unsigned player_ref, unsigned loc_ref)
{
	nd_evt_enter(player_ref, loc_ref);
}
