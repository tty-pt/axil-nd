#include "uapi/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "config.h"
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
do_ban(int fd, int argc __attribute__((unused)), char *argv[]) {
	unsigned player_ref = eng_fd_player(fd), victim_ref;
	char *name = argv[1];

/* INTERMEDIATE (ST.md §27.6(1)): EF_WIZARD is gone, and nothing ever set
 * it, so this gate was already unconditionally taken. Left explicit rather
 * than deleted so the suite still passes for the same reason it passed
 * before -- the region gate lands in the next commit. */
	(void)name;
	goto error;

	victim_ref = player_get(name);

	if (victim_ref == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	if (victim_ref == ROOT)
		goto error;

	OBJ victim;
	corm_get_copy(obj_hd, &victim_ref, &(victim));

	if (victim.type != TYPE_ENTITY)
		goto error;

	ENT evictim = eng_ent_get(victim_ref);
	evictim.flags |= EF_BAN;
	eng_ent_set(player_ref, &evictim);
	nd_writef(victim_ref, "You have been banned.\n");
	eng_nd_close(victim_ref);
	return;
error:
	nd_writef(player_ref, CANTDO_MESSAGE);
}
