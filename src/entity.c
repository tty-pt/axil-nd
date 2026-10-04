#include "uapi/entity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/corm.h>

#include "config.h"
#include "mcp.h"
#include "params.h"
#include "st.h"
#include "uapi/io.h"
#include "uapi/map.h"
#include "uapi/match.h"
#include "view.h"

#include "papi/nd.h"

unsigned ent_hd = -1;
unsigned me = -1;

unsigned eng_me_get(void) {
	return me;
}

ENT eng_ent_get(unsigned ref) {
	ENT ent = { 0 };  /* Zero the whole struct: callers read .flags
	                   * unconditionally, and an unreturned branch used to
	                   * hand them uninitialized stack memory -- which is how
	                   * EF_WIZARD came to be "always false" only by luck. A
	                   * missing row must read as a featureless entity, not
	                   * as whatever was on the stack. */
	const void *__v = corm_get(ent_hd, &ref);
	if (__v)
		ent = *(const ENT *)__v;
	return ent;
}

void eng_ent_set(unsigned ref, ENT *tmp) {
	corm_put(ent_hd, &ref, tmp);
}

void eng_ent_del(unsigned ref) {
	corm_del(ent_hd, &ref);
}

void
eng_enter(unsigned player_ref, unsigned loc_ref, enum exit e)
{
	OBJ player;
	pos_t destpos;
	uint64_t hit_id = 0;
	uint8_t hit_plen = ST_PLEN_ROOT;

	/* The single funnel for all four arrival paths (movement, teleport,
	 * module/vim teleport, room): a ban bites here, on arrival, never at
	 * login. The destination's morton masked to each width IS the
	 * containment test, so no region lookup is needed. Items teleported by
	 * a ruler bypass this (wiz.c) -- correctly, since only players are
	 * excludable. Refuse BEFORE any output: the caller may already have
	 * announced the move (do_teleport's "wrenching"), and the ban notice
	 * reads as the arrival failing, not as silence. */
	eng_map_where(destpos, loc_ref);
	if (st_ban_check(player_ref, pos_morton(destpos), &hit_id,
			&hit_plen)) {
		st_ban_refuse(player_ref, hit_id, hit_plen);
		return;
	}

	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned old_loc_ref = player.location;

	nd_evt_before_leave(player_ref);

	if (e == E_NULL)
		nd_owritef(player_ref, "%s teleports out.\n", player.name);
	else {
		nd_writef(player_ref, "You go %s%s%s.\n", ANSI_FG_BLUE ANSI_BOLD, e_name(e), ANSI_RESET);
		nd_owritef(player_ref, "%s goes %s.\n", player.name, e_name(e));
	}
	eng_object_move(player_ref, loc_ref);
	eng_room_clean(old_loc_ref);
	if (e == E_NULL) {
		nd_writef(player_ref, "Teleported\n");
		nd_owritef(player_ref, "%s teleports in.\n", player.name);
	} else
		nd_owritef(player_ref, "%s comes in from the %s.\n", player.name, e_name(e_simm(e)));

	nd_evt_after_enter(player_ref);
}

/* Every actor pays from their own purse. The EF_WIZARD clause that used to
 * make wizards pay for free is gone with the flag (ST.md §27.6(1)): nothing
 * ever set it, and "the ruler of a region pays other people's bills" has no
 * region to attach to, so there is no scoped version worth having. */
int
eng_payfor(unsigned who_ref, OBJ *who, unsigned cost)
{
	/* who_ref is unused now, and stays in the signature because this is the
	 * public module entry point (nd_api.c:303, nd.payfor) -- a module's idea
	 * of who is paying is still worth passing even though the answer is always
	 * "who". */
	(void)who_ref;

	if (who->value >= cost) {
		who->value -= cost;
		return 1;
	} else {
		return 0;
	}
}

int
eng_controls(unsigned who_ref, unsigned what_ref)
{
	uint64_t wid = 0;
	uint8_t wplen = ST_PLEN_ROOT;

	if (what_ref == NOTHING)
		return 0;

	/* Zombies and puppets use the permissions of their owner */
	OBJ who, what;
	corm_get_copy(obj_hd, &who_ref, &(who));
	if (who.type != TYPE_ENTITY)
		who_ref = who.owner;

	corm_get_copy(obj_hd, &what_ref, &(what));

	/* Ownership of any kind still applies, including puppet ownership. ROOT
	 * and NOTHING are deliberately not ownership here: ref 1 is both ROOT
	 * and the first player, so reading ROOT as an owner would make the first
	 * player own every ROOT-owned room. Those fall through to rulership. */
	if (what.owner != ROOT && what.owner != NOTHING &&
			what.owner == who_ref)
		return 1;
	if (what.owner == ROOT || what.owner == NOTHING) {
		if (st_region_of_obj(what_ref, &wid, &wplen) != 0)
			return 0;
		return st_can_region(who_ref, wid, wplen);
	}

	/* An owner that is a different player keeps authority over possessions.
	 * Another player may be inspected in-region, but their inventory may
	 * not be taken through region authority. */
	if (corm_get(obj_hd, &what.owner)) {
		OBJ owner;

		memset(&owner, 0, sizeof(owner));
		corm_get_copy(obj_hd, &what.owner, &(owner));
		if (owner.type == TYPE_ENTITY) {
			if (what.type != TYPE_ENTITY)
				return 0;
			if (st_region_of_obj(what_ref, &wid, &wplen) != 0)
				return 0;
			return st_can_region(who_ref, wid, wplen);
		}
	}
	return 0;
}

#define BUFF(...) buf_l += snprintf(&buf[buf_l], BUFSIZ - buf_l, __VA_ARGS__)

const char *
eng_unparse(unsigned loc_ref)
{
	static char buf[BUFSIZ];
        size_t buf_l = 0;

	if (loc_ref == NOTHING)
		return "*NOTHING*";

	OBJ loc;
	corm_get_copy(obj_hd, &loc_ref, &(loc));

	BUFF("%s(#%u)", loc.name, loc_ref);

	buf[buf_l] = '\0';
	return buf;
}

void
eng_look_at(unsigned player_ref, unsigned loc_ref)
{
	OBJ player;
	ENT eplayer = eng_ent_get(player_ref);
	OBJ loc;

	if (loc_ref == NOTHING) {
		corm_get_copy(obj_hd, &player_ref, &(player));
		loc_ref = player.location;
		if (loc_ref == NOTHING) {
			eng_fbcp_item(player_ref, loc_ref, 1);
			nd_writef(player_ref, "You see both nothing and everything...\n");
			return;
		}
	}

	/* Phase 4 guard (step 2): observing the same room twice appends
	 * nothing. Sound only because step 1 repaired last_observed on every
	 * obs_hd removal path; a stale room would otherwise read as live and
	 * skip the rejoin put. Replaces the session-hunt duplicate check.
	 * obs_hd is transient, so this guard alone closes the duplicate
	 * factory: no membership scan is needed. */
	if (eplayer.last_observed != loc_ref)
		corm_put(obs_hd, &loc_ref, &player_ref);
	eplayer.last_observed = loc_ref;
	eng_ent_set(player_ref, &eplayer);

	corm_get_copy(obj_hd, &loc_ref, &(loc));
	unsigned thing_ref;

	eng_fbcp_item(player_ref, loc_ref, 1);

	if (loc.type == TYPE_ROOM)
		view(player_ref);

	/* Looking inside another entity needs regional authority over that
	 * entity's region. Ownership is not consulted here: eng_controls is the
	 * authority test for acting on the object, while this is the narrower
	 * test for observing inside it. */
	if (loc_ref != player_ref && loc.type == TYPE_ENTITY) {
		uint64_t lid = 0;
		uint8_t lplen = ST_PLEN_ROOT;

		if (st_region_of_obj(loc_ref, &lid, &lplen) != 0)
			return;
		if (!st_can_region(player_ref, lid, lplen))
			return;
	}

	// use callbacks for mcp like this versus telnet
	unsigned c = corm_iter(contents_hd, &loc_ref, CM_RANGE);
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		loc_ref = *(const unsigned *)kp;
		thing_ref = *(const unsigned *)vp;
		eng_fbcp_item(player_ref, thing_ref, 0);
	}

	nd_twritef(player_ref, "%s\n", eng_unparse(loc_ref));

        char buf[BUFSIZ];
        size_t buf_l = 0;

	unsigned c2 = corm_iter(contents_hd, &loc_ref, CM_RANGE);
	while (corm_next(&kp, &vp, c2)) {
		loc_ref = *(const unsigned *)kp;
		thing_ref = *(const unsigned *)vp;
	/* check to see if there is anything there */
			if (thing_ref == player_ref)
				continue;
			buf_l += snprintf(&buf[buf_l], BUFSIZ - buf_l,
					"%s\r\n", eng_unparse(thing_ref));
	}

	buf[buf_l] = '\0';
	/* Trailing newline, like every other look line: an empty inventory
	 * leaves buf empty, and without the newline the "Contents: " fragment
	 * has no line terminator on the wire -- same framing trap as wall's
	 * shout (speech.c). A reader polling with a timeout discards the
	 * unterminated tail, so the delivery happened but could never match. */
	nd_twritef(player_ref, "Contents: %s\n", buf);
}

#define ADAM_SKEL_REF 0

void
do_look_at(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), thing_ref;
	OBJ player, thing;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";

	if (*name == '\0') {
		corm_get_copy(obj_hd, &player_ref, &(player));
		thing_ref = player.location;
	} else if (
			(thing_ref = eng_ematch_absolute(name)) == NOTHING
			&& (thing_ref = eng_ematch_here(player_ref, name)) == NOTHING
			&& (thing_ref = eng_ematch_me(player_ref, name)) == NOTHING
			&& (thing_ref = eng_ematch_near(player_ref, name)) == NOTHING
			&& (thing_ref = eng_ematch_mine(player_ref, name)) == NOTHING
		  )
	{
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	corm_get_copy(obj_hd, &thing_ref, &(thing));
	switch (thing.type) {
	case TYPE_ROOM:
		view(player_ref);
	default:
		break;
	}
	eng_look_at(player_ref, thing_ref);
	eng_nd_flush(player_ref);
}

int
do_status(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	OBJ obj;
	unsigned player_ref = eng_fd_player(fd);
	corm_get_copy(obj_hd, &player_ref, &(obj));
	nd_writef(player_ref, "%s (%u) type %u owner %u flags %u at %u\n", obj.name, player_ref, obj.type, obj.owner, obj.flags, obj.location);
	nd_evt_status(player_ref);
	/* Same reason do_connect flushes after nd_event_announce() (world.c:865):
	 * eng_nd_write() is a history+dedup buffer, not an append buffer (io.c:180)
	 * -- a differing message flushes the PREVIOUS one and replaces it. So
	 * without this, the last on_status listener's output is still pending
	 * when do_status returns, and the player sees only the line above: a
	 * module that writes in on_status silently never appears. Verified both
	 * ways: without the flush, `status` followed by 2.5s of silence printed
	 * no "Level"; with it, the line is on the wire immediately. */
	eng_nd_flush(player_ref);
	return 0;
}
