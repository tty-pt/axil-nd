#include "uapi/object.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <ttypt/corm.h>

#include "config.h"
#include "mcp.h"
#include "noise.h"
#include "params.h"
#include "player.h"
#include "st.h"
#include "uapi/skel.h"
#include "uapi/entity.h"
#include "uapi/io.h"
#include "uapi/map.h"
#include "uapi/match.h"
#include "uapi/type.h"
#include "papi/nd.h"

char std_db[BUFSIZ];
char std_db_ok[BUFSIZ];
unsigned obj_hd, contents_hd, obs_hd;

int eng_obj_exists(unsigned ref) {
	/* "Exists" means present. This was `== NULL`, i.e. TRUE when the row was
	 * ABSENT, which inverted its one internal caller
	 * (eng_ematch_absolute, match.c): every real ref tested false and was
	 * discarded as NOTHING, while every absent ref tested true and was handed
	 * back as a match -- so `teleport #<anything>` fell through to
	 * eng_ematch_at's contents scan and, on a row that does not exist,
	 * corm_get_copy aborted the daemon. Measured: `teleport #1823110` killed
	 * the process with SIGABRT (heap corruption, which
	 * signal(SIGSEGV, close_all) does not catch).
	 *
	 * No module calls this -- nd.obj_exists is the only export and nothing in
	 * the tree or the probes uses it -- so flipping the sense here is a fix,
	 * not a compatibility break. */
	return corm_get(obj_hd, &ref) != NULL;
}

static void
del_dup_value(unsigned hd, unsigned key, unsigned value)
{
	/* corm_del_value removes only the first match; loop until none remain.
	 * Replaces a full teardown plus n-1 sort-inserts and a malloc with a
	 * single unlink per duplicate. With the duplicate factory closed (the
	 * eng_look_at guard), this is single-pass in practice and correct by
	 * construction regardless. */
	while (corm_del_value(hd, &key, &value))
		;
}

unsigned
eng_object_new(OBJ *newobj)
{
	unsigned flags = newobj->flags;
	memset(newobj, 0, sizeof(OBJ));
	newobj->location = NOTHING;
	newobj->owner = ROOT;
	newobj->flags = flags;
	return corm_put(obj_hd, NULL, newobj);
}

ucoord_t biome_rain_floor[16] = {
	0, 0, 0, 0,
	128, 128, 128, 128,
	256, 256, 256, 256,
	384, 384, 384, 384,
};

ucoord_t biome_rain_ceil[16] = {
	128, 128, 128, 128,
	256, 256, 256, 256,
	384, 384, 384, 384,
	512, 512, 512, 512,    
};

coord_t biome_temp_floor[16] = {
	-41, -41, -41, -41,
	11, 11, 11, 11,
	64, 64, 64, 64,
	117, 117, 117, 117,
};

coord_t biome_temp_ceil[16] = {
	11, 11, 11, 11,
	64, 64, 64, 64,
	117, 117, 117, 117,
	170, 170, 170, 170,
};

unsigned _bio_idx(coord_t tmp_f, coord_t tmp_c, ucoord_t rain_f, ucoord_t rain_c, coord_t tmp, ucoord_t rain);

static inline unsigned
biome_art_idx(struct bio *bio) {
	return 1 + _bio_idx(biome_rain_floor[bio->bio_idx],
		biome_rain_ceil[bio->bio_idx],
		biome_temp_floor[bio->bio_idx],
		biome_temp_ceil[bio->bio_idx],
		bio->rn,
		bio->tmp);
}

// there are 6 wolf art images

static inline unsigned
art_idx(OBJ *obj) {
	SKEL skel;
	corm_get_copy(skel_hd, &obj->skid, &(skel));
	return 1 + (random() % (skel.max_art ? skel.max_art : 1));
}

unsigned
eng_object_add(OBJ *nu, unsigned skel_id, unsigned where_ref, uint64_t v, unsigned flags)
{
	SKEL skel;
	corm_get_copy(skel_hd, &skel_id, &(skel));
	unsigned nu_ref = eng_object_new(nu);
	memset(nu, 0, sizeof(OBJ));
	strlcpy(nu->name, skel.name, sizeof(nu->name));
	nu->skid = skel_id;
	if (where_ref == NOTHING)
		where_ref = nu_ref;
	nu->location = where_ref;
	nu->owner = ROOT;
	nu->type = skel.type;
	if (where_ref != NOTHING)
		corm_put(contents_hd, &nu->location, &nu_ref);
	fprintf(stderr, "eng_object_add %u -> %u\n", nu_ref, where_ref);

	switch (skel.type) {
	case TYPE_ENTITY:
		{
			ENT ent;
			memset(&ent, 0, sizeof(ent));
			ent.flags = ((SENT *) &skel.data)->flags;
			if (flags & OF_PLAYER)
				nu->flags |= OF_PLAYER;
			ent.select = 0;
			eng_object_drop(nu_ref, skel_id);
			ent.home = 1;
			eng_ent_set(nu_ref, &ent);
			nu->art_id = art_idx(nu);
		}

		break;
        case TYPE_ROOM:
		{
			struct bio *bio = (struct bio *) v;
			ROO *rnu = (ROO *) &nu->data;
			rnu->exits = rnu->doors = 0;
			rnu->flags = RF_TEMP;
			nu->art_id = biome_art_idx(bio);
		}

		break;

	default:
		 break;
	}

	corm_put(obj_hd, &nu_ref, nu);
	nd_evt_add(nu_ref, nu->type, v);

	if (skel.type != TYPE_ROOM)
		eng_mcp_content_in(where_ref, nu_ref);

	// needed for retrieval (FIXME)
	corm_get_copy(obj_hd, &nu_ref, nu);
	return nu_ref;
}

char *
eng_object_art(unsigned thing_ref)
{
	OBJ thing;
	static char art[BUFSIZ];
	char typestr[BUFSIZ];
	char *type = NULL;
	char *void_art = "biome/void/1.jpeg";

	if (thing_ref == NOTHING)
		return void_art;

	corm_get_copy(obj_hd, &thing_ref, &(thing));
	switch (thing.type) {
	case TYPE_ENTITY:
		type = "entity";
		snprintf(art, sizeof(art), "%s/%s/%u.jpeg", type, thing.art_id && thing.flags & OF_PLAYER ? "avatar" : thing.name, thing.art_id);
		return art;
	case TYPE_ROOM:
			if (!eng_map_has(thing_ref))
				return void_art;

			type = "biome";
			break;
	default:
		 /* "room" is id 1 in the transient type_hd — the host seeds it
		  * first, before any module loads. See world.c. */
		 const void *__v2 = corm_get(type_hd + 1, &thing.type);
		 if (__v2)
			 strlcpy(typestr, (const char *)__v2, sizeof(typestr));
		 type = typestr; break;
	}

	snprintf(art, sizeof(art), "%s/%s/%u.jpeg", type, thing.name, thing.art_id);
	return art;
}

void
objects_init(void)
{
	unsigned ref;
	unsigned c = corm_iter(obj_hd, NULL, 0);
	OBJ oi;
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		ref = *(const unsigned *)kp;
		oi = *(const OBJ *)vp;
		/* contents_hd is transient: empty at boot, so this is always a
		 * fresh insert and a complete rebuild (obj_hd is the source). */
		if (oi.location != NOTHING)
			corm_put(contents_hd, &oi.location, &ref);
		if (oi.type != TYPE_ENTITY)
			continue;

		ENT ent = eng_ent_get(ref);
		/* obs_hd is transient, so an empty obs map makes NOTHING the
		 * truthful value; this also re-enables eng_object_move's
		 * last_observed cleanup, which is the invariant the
		 * eng_look_at same-room guard depends on. */
		ent.last_observed = NOTHING;
		corm_put(ent_hd, &ref, &ent);
		if (oi.flags & OF_PLAYER)
			player_put(oi.name, ref);
	}
}

unsigned
eng_object_copy(OBJ *nu, unsigned old_ref)
{
	OBJ old;
	corm_get_copy(obj_hd, &old_ref, &(old));
	unsigned ref = eng_object_new(nu);
	strlcpy(nu->name, old.name, sizeof(nu->name));
	nu->location = NOTHING;
	nu->owner = old.owner;
        return ref;
}

void
objects_update(double dt)
{
	OBJ obj;
	unsigned obj_ref;
	unsigned c = corm_iter(obj_hd, NULL, 0);
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		obj_ref = *(const unsigned *)kp;
		obj = *(const OBJ *)vp;
		nd_evt_update(obj_ref, obj.type, dt);
	}
}

void
eng_object_move(unsigned what_ref, unsigned where_ref)
{
	unsigned last_loc;
	OBJ what;
	corm_get_copy(obj_hd, &what_ref, &(what));

	last_loc = what.location;
	/* if (last_loc == where_ref) */
	/* 	return; */

	fprintf(stderr, "eng_object_move %u %s -> %u\n", what_ref, what.name, where_ref);
	eng_mcp_content_out(last_loc, what_ref);
        if (last_loc != NOTHING)
		del_dup_value(contents_hd, what.location, what_ref);

	unsigned c = corm_iter(obs_hd, &what_ref, CM_RANGE);
	unsigned first_ref;
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		what_ref = *(const unsigned *)kp;
		first_ref = *(const unsigned *)vp;
		ENT efirst = eng_ent_get(first_ref);
		efirst.last_observed = what.location;
		eng_ent_set(first_ref, &efirst);
	}
	corm_del_all(obs_hd, &what_ref);

	/* test for special cases */
	if (where_ref == NOTHING) {
		/* Collect-then-delete, in passes: the old code deleted inside the
		 * corm_next loop, mutating the index under its own cursor, and it
		 * trusted every pair. A STALE pair -- an object the index still
		 * files here after a move dropped only the live one -- then deleted
		 * a live object: "room 5 at 0 0 0 1" aborted boot B by deleting
		 * quirinpa out from under the session, because room 2's contents
		 * still listed a player standing in room 5. So each candidate is
		 * verified against its row: gone rows and rows filed elsewhere get
		 * the pair dropped, not the object deleted. A self-pair would
		 * recurse forever, so it is dropped too. Every pass removes at
		 * least one pair, so this terminates. */
		for (;;) {
			unsigned kids[64];
			unsigned nkids = 0;
			unsigned c = corm_iter(contents_hd, &what_ref, CM_RANGE);
			const void *kp, *vp;
			while (corm_next(&kp, &vp, c) && nkids < 64)
				kids[nkids++] = *(const unsigned *)vp;
			corm_fin(c);
			if (!nkids)
				break;
			for (unsigned i = 0; i < nkids; i++) {
				unsigned child_ref = kids[i];
				const OBJ *child = corm_get(obj_hd, &child_ref);
				if (!child || child->location != what_ref ||
				    child_ref == what_ref) {
					del_dup_value(contents_hd, what_ref,
					    child_ref);
					continue;
				}
				eng_object_move(child_ref, NOTHING);
			}
		}

		switch (what.type) {
		case TYPE_ENTITY: {
			/* This branch returns before the eng_object_move cleanup
			 * below, so a player deleted mid-session who had observed
			 * a room would strand its obs pair and CBUG
			 * fbcp_observers' corm_get_copy on the gone OBJ. Probe —
			 * cleanup must not abort — and drop the pair. last_observed
			 * is truthful for an entity holding a pair, so the NOTHING
			 * guard is sound. */
			const void *ev = corm_get(ent_hd, &what_ref);
			if (ev && ((const ENT *)ev)->last_observed != NOTHING)
				del_dup_value(obs_hd,
				    ((const ENT *)ev)->last_observed, what_ref);
			eng_ent_del(what_ref);
			break;
		}
		case TYPE_ROOM:
			eng_map_delete(what_ref);
		}
		eng_mcp_content_out(last_loc, what_ref);
		corm_del(obj_hd, &what_ref);

		nd_evt_del(what_ref, what.type);
		return;
	}

	what.location = where_ref;
	corm_put(obj_hd, &what_ref, &what);

	if (what.type == TYPE_ENTITY) {
		ENT ewhat = eng_ent_get(what_ref);
		if (ewhat.last_observed != NOTHING) {
			del_dup_value(obs_hd, ewhat.last_observed, what_ref);
			/* Repair (Phase 4 step 1): detached from obs_hd, so not
			 * observing anything. NOTHING keeps last_observed
			 * consistent with membership; without it the guard
			 * below would read a stale room as live and skip the
			 * rejoin put, losing all notices for it. */
			ewhat.last_observed = NOTHING;
			eng_ent_set(what_ref, &ewhat);
		}
	}

	corm_put(contents_hd, &where_ref, &what_ref);
	nd_evt_leave(what_ref, last_loc);
	nd_evt_enter(what_ref, where_ref);
	eng_mcp_content_in(where_ref, what_ref);
}

struct icon
eng_object_icon(unsigned player_ref, unsigned what_ref)
{
	OBJ what;
	corm_get_copy(obj_hd, &what_ref, &(what));
	return nd_evt_icon(what_ref, what.type, player_ref);
}

static inline int
ok_name(const char *name)
{
	return (name
			&& *name
			&& *name != NUMBER_TOKEN
			&& !strchr(name, ' ')
			&& !strchr(name, '\r')
			&& !strchr(name, ESCAPE_CHAR)
			&& strcmp(name, "me")
			&& strcmp(name, "home")
			&& strcmp(name, "here"));
}

void
do_clone(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), thing_ref;
	uint64_t tid = 0;
	uint8_t tplen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";

	if (
			(thing_ref = eng_ematch_absolute(name)) == NOTHING
			&& (thing_ref = eng_ematch_mine(player_ref, name)) == NOTHING
			&& (thing_ref = eng_ematch_near(player_ref, name)) == NOTHING
	   )
	{
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	/* Region of the cloned object, then source control: cloning what is in
	 * scope is not the same as controlling the source. Both have to pass. */
	if (st_region_of_obj(thing_ref, &tid, &tplen) != 0) {
		st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
		return;
	}
	if (!st_in_scope(player_ref, 0, ST_SEL_UNSET, tid, tplen)) {
		st_refuse_region(player_ref, tid, tplen);
		return;
	}

	OBJ thing;
	corm_get_copy(obj_hd, &thing_ref, &(thing));

	if(!eng_controls(player_ref, thing_ref)) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	OBJ clone;
	unsigned clone_ref = eng_object_new(&clone);

	strlcpy(clone.name, thing.name, sizeof(clone.name));
	clone.location = player_ref;
	clone.owner = player.owner;
	clone.value = thing.value;

	switch (thing.type) {
		case TYPE_ROOM:
			{
				ROO *rclone = (ROO *) &clone.data;
				rclone->exits = rclone->doors = 0;
			}
			break;
		case TYPE_ENTITY:
			{
				ENT eclone = eng_ent_get(clone_ref);
				eclone.home = eng_ent_get(thing_ref).home;
				/* NOTHING, not zero: objects_init documents a missing
				 * last_observed as NOTHING, and the eng_look_at
				 * same-room guard depends on it. Zero is ref 0, a real
				 * row, and would read as "last seen in object 0". */
				eclone.last_observed = NOTHING;
				eng_ent_set(clone_ref, &eclone);
			}
			break;
	}

	clone.type = thing.type;

	corm_put(obj_hd, &clone_ref, &clone);
	/* A clone is a birth like any other: without nd_evt_add no module
	 * initializes its rows (nd-mortal's mortal row, nd-spell's caster row),
	 * and the next world tick reads the absence as data. Measured: a cloned
	 * human segfaulted the daemon in nd-spell's debuf_notify two ticks
	 * later, via mortal_update on a missing mortal row. nd_evt_clone fires
	 * too, but no module implements on_clone -- it announces, it does not
	 * initialize. The birth value is the source's own value, the same thing
	 * eng_object_add hands on_add for a created object. */
	nd_evt_add(clone_ref, clone.type, clone.value);
	nd_evt_clone(thing_ref, clone_ref);
	eng_object_move(clone_ref, player_ref);
	eng_nd_flush(player_ref);
}

void
do_create(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd),
		 ref;
	unsigned long long skid;
	unsigned long long value = 0;
	uint64_t cid = 0;
	uint8_t cplen = ST_PLEN_ROOT;
	uint64_t v = 0;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char *skid_text = (argc > 2 && argv[2]) ? argv[2] : "";
	char *value_text = (argc > 3 && argv[3]) ? argv[3] : "";
	char *end = NULL;
	OBJ obj;

	if (!*name || !*skid_text || !ok_name(name)) {
		nd_writef(player_ref, "Syntax: create name skel_id [v]\n");
		eng_nd_flush(player_ref);
		return;
	}

	skid = 0;
	errno = 0;
	if (*skid_text == '-' || isspace((unsigned char)*skid_text)) {
		nd_writef(player_ref, "Syntax: create name skel_id [v]\n");
		eng_nd_flush(player_ref);
		return;
	}
	skid = strtoull(skid_text, &end, 10);
	if (errno || !end || *end || skid > UINT_MAX) {
		nd_writef(player_ref, "Syntax: create name skel_id [v]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (*value_text) {
		if (*value_text == '-' ||
				isspace((unsigned char)*value_text)) {
			nd_writef(player_ref, "Syntax: create name skel_id [v]\n");
			eng_nd_flush(player_ref);
			return;
		}
		errno = 0;
		value = strtoull(value_text, &end, 10);
		if (errno || !end || *end) {
			nd_writef(player_ref, "Syntax: create name skel_id [v]\n");
			eng_nd_flush(player_ref);
			return;
		}
		v = (uint64_t)value;
	}

	/* The new object lands in the creator's inventory, so there is no target
	 * placement to authorize. The honest scope is the region the creator is
	 * standing in, checked against the union of regions they rule. */
	if (st_region_of_player(player_ref, &cid, &cplen) != 0) {
		st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
		return;
	}
	if (!st_in_scope(player_ref, 0, ST_SEL_UNSET, cid, cplen)) {
		st_refuse_region(player_ref, cid, cplen);
		return;
	}

	/* A missing skeleton must not reach eng_object_add: it copies the row
	 * without checking, so an absent row would seed the new object from
	 * uninitialized stack memory. A room skeleton must not reach it either:
	 * the TYPE_ROOM branch reads `v` as a `struct bio *`, which a command
	 * line cannot truthfully supply. Rooms are made with `room`. */
	SKEL skel;
	unsigned skid_ref = (unsigned)skid;
	if (!corm_get(skel_hd, &skid_ref)) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}
	memset(&skel, 0, sizeof(skel));
	corm_get_copy(skel_hd, &skid_ref, &skel);
	if (skel.type == TYPE_ROOM) {
		nd_writef(player_ref, "Rooms are made with `room`, not `create`.\n");
		eng_nd_flush(player_ref);
		return;
	}

	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));

	ref = eng_object_add(&obj, (unsigned)skid, player_ref, v, 0);
	obj.owner = player.owner;

	corm_put(obj_hd, &ref, &obj);
	nd_writef(player_ref, "Created.\n");
	eng_nd_flush(player_ref);
}

void
do_name(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char *name = argv[1];
	char *newname = argv[2];
	unsigned thing_ref = eng_ematch_all(player_ref, name);

	if (thing_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	if (!eng_controls(player_ref, thing_ref) || !*newname || !ok_name(newname)) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	OBJ thing;
	corm_get_copy(obj_hd, &thing_ref, &(thing));
	strlcpy(thing.name, newname, sizeof(thing.name));
	corm_put(obj_hd, &thing_ref, &thing);
}

void
do_chown(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), owner_ref, thing_ref;
	uint64_t tid = 0;
	uint8_t tplen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char *newowner = (argc > 2 && argv[2]) ? argv[2] : "";

	if (!*name || !*newowner ||
			(thing_ref = eng_ematch_all(player_ref, name)) == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	/* Ownership can only move inside the actor's authority. This gate comes
	 * before the old containment/entity rules: those say whether the move is
	 * shaped correctly, while the region says whether this actor may touch
	 * the object at all. */
	if (st_region_of_obj(thing_ref, &tid, &tplen) != 0) {
		st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
		return;
	}
	if (!st_can_region(player_ref, tid, tplen)) {
		st_refuse_region(player_ref, tid, tplen);
		return;
	}

	OBJ player, thing;
	corm_get_copy(obj_hd, &player_ref, &(player));

	owner_ref = *newowner && strcmp(newowner, "me") ? player_get(newowner) : player.owner;
	if (owner_ref == NOTHING)
		goto error;

	corm_get_copy(obj_hd, &thing_ref, &(thing));

	if (thing.type == TYPE_ENTITY ||
			((thing.type == TYPE_ROOM && player.location != thing_ref)
				     || (thing.type != TYPE_ROOM && thing.location != player_ref )))
		goto error;

	thing.owner = owner_ref;
	corm_put(obj_hd, &thing_ref, &thing);
	eng_nd_flush(player_ref);
	return;

error:
	nd_writef(player_ref, CANTDO_MESSAGE);
	eng_nd_flush(player_ref);
}

void
do_recycle(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char *name = argv[1];
	unsigned thing_ref;
	OBJ thing;

	if (
			(thing_ref = eng_ematch_absolute(name)) == NOTHING
			&& (thing_ref = eng_ematch_near(player_ref, name)) == NOTHING
			&& (thing_ref = eng_ematch_mine(player_ref, name)) == NOTHING
	   )
	{
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	corm_get_copy(obj_hd, &thing_ref, &(thing));

	if (!eng_controls(player_ref, thing_ref) || thing.owner != player_ref) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	eng_object_move(thing_ref, NOTHING);
}

void
do_get(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), thing_ref, cont_ref;
	char *what = argv[1];
	char *obj = argv[2];

	if (
			(thing_ref = eng_ematch_near(player_ref, what)) == NOTHING
			&& (thing_ref = eng_ematch_mine(player_ref, what)) == NOTHING
	   )
	{
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	cont_ref = thing_ref;
	OBJ player, thing, cont;
	corm_get_copy(obj_hd, &player_ref, &(player));
	corm_get_copy(obj_hd, &cont_ref, &(thing));
	cont = thing;

	if (obj && *obj) {
		thing_ref = eng_ematch_at(player_ref, cont_ref, obj);
		if (thing_ref == NOTHING) {
			nd_writef(player_ref, NOMATCH_MESSAGE);
			return;
		}
		corm_get_copy(obj_hd, &thing_ref, &(thing));
		if (cont.type == TYPE_ENTITY)
			goto error;
	}

	if (thing.location == player_ref
			|| thing_ref == player_ref
			|| thing_ref == player.location)
		goto error;

	switch (thing.type) {
	case TYPE_ENTITY: if (player_ref == ROOT)
				  break;
	case TYPE_ROOM: goto error;
	default: break;
	}

	if (nd_evt_get(player_ref, thing_ref))
		goto error;

	eng_object_move(thing_ref, player_ref);
	return;
error:
	nd_writef(player_ref, CANTDO_MESSAGE);
}

void
do_drop(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), thing_ref, cont_ref;
	OBJ player, cont, thing;
	corm_get_copy(obj_hd, &player_ref, &(player));
	char *name = argv[1];
	char *obj = argv[2];

	if ((thing_ref = eng_ematch_mine(player_ref, name)) == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	cont_ref = player.location;
	if (
			obj && *obj
			&& (cont_ref = eng_ematch_mine(player_ref, obj)) == NOTHING
			&& (cont_ref = eng_ematch_near(player_ref, obj)) == NOTHING
	   )
	{
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}
        
	if (thing_ref == cont_ref)
		goto error;

	corm_get_copy(obj_hd, &cont_ref, &(cont));

	eng_object_move(thing_ref, cont_ref);
	corm_get_copy(obj_hd, &thing_ref, &(thing));

	if (cont.type == TYPE_ENTITY) {
		nd_writef(cont_ref, "%s hands you %s.\n", player.name, thing.name);
		return;
	}

	nd_owritef(player_ref, "%s drops %s.\n", player.name, thing.name);
	return;
error:
	nd_writef(player_ref, CANTDO_MESSAGE);
}
