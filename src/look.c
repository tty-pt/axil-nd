#include "uapi/io.h"

#include "mcp.h"
#include "player.h"
#include "uapi/entity.h"
#include "uapi/match.h"
#include "uapi/type.h"

void
do_examine(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	char *name = argv[1];
	unsigned thing_ref;

	if (*name == '\0')
		thing_ref = player.location;
	else if ((thing_ref = eng_ematch_all(player_ref, name)) == NOTHING) {
		nd_writef(player_ref, NOMATCH_MESSAGE);
		return;
	}

	OBJ thing, owner;
	corm_get_copy(obj_hd, &thing_ref, &(thing));
	corm_get_copy(obj_hd, &thing.owner, &(owner));

	if (!eng_controls(player_ref, thing_ref)) {
		nd_writef(player_ref, "Owner: %s\n", owner.name);
		return;
	}

	nd_writef(player_ref, "%s (#%d) Owner: %s  Value: %d\n",
			eng_unparse(thing_ref), thing_ref, owner.name, thing.value);

	/* show him the contents */
	unsigned c = corm_iter(contents_hd, &thing_ref, CM_RANGE);
	unsigned content_ref;
	const void *kp, *vp;

	while (corm_next(&kp, &vp, c)) {
		thing_ref = *(const unsigned *)kp;
		content_ref = *(const unsigned *)vp;
		nd_writef(player_ref, eng_unparse(content_ref));
	}

	switch (thing.type) {
	case TYPE_ROOM:
		{
			ROO *rthing = (ROO *) &thing.data;
			nd_writef(player_ref, "Exits: %hhx Doors: %hhx\n", rthing->exits, rthing->doors);
		}
		break;
	case TYPE_ENTITY:
		{
			ENT ething = eng_ent_get(thing_ref);

			nd_writef(player_ref, "Home: %s, Flags: %d\n", eng_unparse(ething.home), ething.flags);

			/* print location if player can link to it */
		}
		break;
	default: break;
	}

	nd_evt_examine(player_ref, thing_ref, thing.type);
	nd_writef(player_ref, "Location: %s\n", eng_unparse(thing.location));
}


void
do_inventory(int fd, int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	unsigned player_ref = eng_fd_player(fd);
	OBJ player;

	eng_look_at(player_ref, player_ref);
	corm_get_copy(obj_hd, &player_ref, &(player));
	nd_writef(player_ref, "You have %d %s.\n", player.value,
			plural_maybe("shekel", player.value));
}

void
do_owned(int fd, int argc __attribute__((unused)), char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), victim_ref, oi_ref;
	int total = 0;

	/* argv goes away with the region gate; the named form needs it back. */
	(void)argv;

/* INTERMEDIATE (ST.md §27.6(1)): EF_WIZARD is gone, and nothing ever set
 * it, so this gate was already unconditionally taken. Left explicit rather
 * than deleted so the suite still passes for the same reason it passed
 * before -- the region gate lands in the next commit. */
	victim_ref = player_ref;

	OBJ victim, oi;
	corm_get_copy(obj_hd, &victim_ref, &(victim));
	unsigned c = corm_iter(obj_hd, NULL, 0);
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		oi_ref = *(const unsigned *)kp;
		oi = *(const OBJ *)vp;
		if (oi.owner == victim.owner) {
			nd_writef(player_ref, "%s\n", eng_unparse(oi_ref));
			total++;
		}
	}
	nd_writef(player_ref, "%d objects found.\n", total);
}
