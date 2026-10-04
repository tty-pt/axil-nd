#include "uapi/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <ttypt/xy.h>

#include "config.h"
#include "st.h"
#include "uapi/entity.h"
#include "uapi/match.h"
#include "player.h"

void
do_teleport(int fd, int argc __attribute__((unused)), char *argv[]) {
	unsigned player_ref = eng_fd_player(fd), victim_ref, destination_ref;
	char *arg1, *arg2, *to;

	/* Both argv[1] and argv[2] are read before anything proves they are
	 * there. axil delivers a bare verb as argc == 2 with an empty argv[1],
	 * so `argc < 2` does not catch it (measured: bare `create` reached its
	 * gate, not its Usage line) -- the guard has to be on the string, which
	 * is also what makes argv[2] safe to dereference below. axil additionally
	 * sets argv[argc] = "" (libaxil.c cmd_proc), but relying on that for
	 * argv[2] means relying on an off-by-one nobody has measured. */
	if (!argv[1] || !*argv[1]) {
		nd_writef(player_ref, "Usage: teleport [<victim>] <destination>\n");
		eng_nd_flush(player_ref);
		return;
	}
	arg1 = argv[1];
	arg2 = (argv[2] && *argv[2]) ? argv[2] : "";

	/* get victim, destination */
	if (*arg2 == '\0') {
		victim_ref = player_ref;
		to = arg1;
	} else if ((victim_ref = eng_ematch_all(player_ref, arg1)) == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	} else
		to = arg2;

	OBJ victim;
	corm_get_copy(obj_hd, &victim_ref, &(victim));

	destination_ref = eng_ematch_all(player_ref, to);

	if (destination_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	OBJ destination;
	corm_get_copy(obj_hd, &destination_ref, &(destination));

	if (victim_ref == destination_ref || destination.location == victim_ref)
		goto error;

	switch (victim.type) {
	case TYPE_ENTITY:
		if (!eng_controls(player_ref, victim_ref) ||
				!eng_controls(player_ref, destination_ref) ||
				!eng_controls(player_ref, victim.location) ||
				destination.type != TYPE_ROOM)
			goto error;
		nd_writef(victim_ref, "You feel a wrenching sensation...\n");
		eng_enter(victim_ref, destination_ref, E_NULL);
		eng_nd_flush(victim_ref);
		eng_nd_flush(player_ref);
		return;
	case TYPE_ROOM:
		goto error;
	default:
		if (!(
			eng_controls(player_ref, destination_ref) &&
			(eng_controls(player_ref, victim_ref) || eng_controls(player_ref, victim.location)) )
		   )
			goto error;
		eng_object_move(victim_ref, destination_ref);
		eng_nd_flush(player_ref);
		return;
	}
error:
	nd_writef(player_ref, CANTDO_MESSAGE);
	eng_nd_flush(player_ref);
}

void
do_ban(int fd, int argc, char *argv[]) {
	unsigned player_ref = eng_fd_player(fd), victim_ref;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char place[64];
	OBJ victim;

	if (!*name) {
		nd_writef(player_ref, "Usage: ban <player> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	victim_ref = player_get(name);
	if (victim_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}
	if (victim_ref == ROOT) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}
	corm_get_copy(obj_hd, &victim_ref, &(victim));
	if (victim.type != TYPE_ENTITY) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}

	/* argv[2] names a world outright; without it, the caller's own region.
	 * One selector dialect, shared with wall (st_cmd_region). */
	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Usage: ban <player> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}

	st_ban_put(victim_ref, id, plen, player_ref);
	st_ban_place(id, plen, place, sizeof(place));
	nd_writef(player_ref, "Banned %s from %s.\n", victim.name, place);
	nd_writef(victim_ref, "You have been banned from %s.\n", place);
	eng_nd_flush(player_ref);
	eng_nd_flush(victim_ref);
}

void
do_unban(int fd, int argc, char *argv[]) {
	unsigned player_ref = eng_fd_player(fd), victim_ref, banner = NOTHING;
	uint64_t id = 0;
	uint8_t plen = ST_PLEN_ROOT;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";
	char place[64];
	OBJ victim;

	if (!*name) {
		nd_writef(player_ref, "Usage: unban <player> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	victim_ref = player_get(name);
	if (victim_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		eng_nd_flush(player_ref);
		return;
	}
	corm_get_copy(obj_hd, &victim_ref, &(victim));

	if (st_cmd_region(player_ref, argc, argv, 2, &id, &plen) != XY_OK) {
		nd_writef(player_ref, "Usage: unban <player> [world]\n");
		eng_nd_flush(player_ref);
		return;
	}
	st_ban_place(id, plen, place, sizeof(place));
	if (!st_ban_lookup(victim_ref, id, plen, &banner)) {
		nd_writef(player_ref, "%s is not banned from %s.\n",
			victim.name, place);
		eng_nd_flush(player_ref);
		return;
	}
	/* The original banner or anyone ruling the region now may lift it: a
	 * ban must not outlive the authority that could remove it, and a new
	 * ruler must not be stuck with the old ruler's bans. */
	if (banner != player_ref && !st_can_region(player_ref, id, plen)) {
		st_refuse_region(player_ref, id, plen);
		return;
	}

	st_ban_del(victim_ref, id, plen);
	nd_writef(player_ref, "Unbanned %s from %s.\n", victim.name, place);
	nd_writef(victim_ref, "You have been unbanned from %s.\n", place);
	eng_nd_flush(player_ref);
	eng_nd_flush(victim_ref);
}
