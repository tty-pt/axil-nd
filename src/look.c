#include "uapi/io.h"

#include "mcp.h"
#include "player.h"
#include "st.h"
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
do_owned(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd), victim_ref, oi_ref;
	int total = 0;
	char *name = (argc > 1 && argv[1]) ? argv[1] : "";

	victim_ref = player_ref;
	if (*name) {
		if (strcmp(name, "me") == 0) {
			victim_ref = player_ref;
		} else {
			victim_ref = eng_ematch_player(name);
			if (victim_ref == NOTHING) {
				nd_writef(player_ref, NOMATCH_MESSAGE);
				eng_nd_flush(player_ref);
				return;
			}
		}
	}

	if (victim_ref != player_ref) {
		uint64_t vid = 0;
		uint8_t vplen = ST_PLEN_ROOT;

		if (st_region_of_obj(victim_ref, &vid, &vplen) != 0) {
			st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
			return;
		}
		if (!st_can_region(player_ref, vid, vplen)) {
			st_refuse_region(player_ref, vid, vplen);
			return;
		}
	}

	OBJ victim, oi;
	corm_get_copy(obj_hd, &victim_ref, &(victim));
	unsigned c = corm_iter(obj_hd, NULL, 0);
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		oi_ref = *(const unsigned *)kp;
		oi = *(const OBJ *)vp;
		if (oi.owner != victim.owner)
			continue;
		if (victim_ref != player_ref) {
			uint64_t oid;
			uint8_t oplen;

			if (st_region_of_obj(oi_ref, &oid, &oplen) != 0)
				continue;
			if (!st_in_scope(player_ref, 0, ST_SEL_UNSET, oid,
					oplen))
				continue;
		}
		nd_writef(player_ref, "%s\n", eng_unparse(oi_ref));
		total++;
	}
	corm_fin(c);
	nd_writef(player_ref, "%d objects found.\n", total);
	eng_nd_flush(player_ref);
}
