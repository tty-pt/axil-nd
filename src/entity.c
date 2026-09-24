#include "uapi/entity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <ttypt/corm.h>

#include "config.h"
#include "mcp.h"
#include "params.h"
#include "st.h"
#include "uapi/io.h"
#include "uapi/match.h"
#include "view.h"

#include "papi/nd.h"

unsigned ent_hd = -1;
unsigned me = -1;

unsigned eng_me_get(void) {
	return me;
}

ENT eng_ent_get(unsigned ref) {
	ENT ent;
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
	corm_get_copy(obj_hd, &player_ref, &(player));
	unsigned old_loc_ref = player.location;

	call_on_before_leave(player_ref);

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

	call_on_after_enter(player_ref);
}

int
eng_payfor(unsigned who_ref, OBJ *who, unsigned cost)
{
	if (eng_ent_get(who_ref).flags & EF_WIZARD)
		return 1;

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
	if (what_ref == NOTHING)
		return 0;

	/* Zombies and puppets use the permissions of their owner */
	OBJ who, what;
	corm_get_copy(obj_hd, &who_ref, &(who));
	if (who.type != TYPE_ENTITY)
		who_ref = who.owner;

	corm_get_copy(obj_hd, &what_ref, &(what));

	/* Wizard eng_controls everything */
	if (eng_ent_get(who_ref).flags & EF_WIZARD) {
		if(what.owner == ROOT && who_ref == ROOT)
			return 0;
		else
			return 1;
	}

	/* owners control their own stuff */
	return (who_ref == what.owner);
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

        if (loc_ref != player_ref && loc.type == TYPE_ENTITY && !(eplayer.flags & EF_WIZARD))
                return;

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
        nd_twritef(player_ref, "Contents: %s", buf);
}

#define ADAM_SKEL_REF 0

void
do_look_at(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd), thing_ref;
	OBJ player, thing;
	char *name = argv[1];

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
}

int
do_status(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	OBJ obj;
	unsigned player_ref = eng_fd_player(fd);
	corm_get_copy(obj_hd, &player_ref, &(obj));
	nd_writef(player_ref, "%s (%u) type %u owner %u flags %u at %u\n", obj.name, player_ref, obj.type, obj.owner, obj.flags, obj.location);
	call_on_status(player_ref);
	return 0;
}
