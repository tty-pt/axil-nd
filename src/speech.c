#include "uapi/io.h"

#include <stdio.h>
#include <string.h>
#include <ttypt/xy.h>

#include "config.h"
#include "player.h"
#include "st.h"
#include "uapi/entity.h"
#include "uapi/match.h"

/* Commands which involve speaking */
static char *
wall_message(int argc, char *argv[], int start)
{
	static char message[BUFFER_LEN];
	int rem = sizeof(message);
	int i;

	memset(message, '\0', sizeof(message));
	for (i = start; i < argc; i++) {
		const char *word = argv[i] ? argv[i] : "";
		int ret = snprintf(message + sizeof(message) - rem, rem, " %s",
			word);

		if (ret <= 0)
			return message;
		rem -= ret;
	}

	return message;
}

char *
argscat(int argc, char *argv[]) {
	static char message[BUFFER_LEN];
	int rem = sizeof(message);
	memset(message, '\0', sizeof(message));
	
	for (int i = 1; i < argc; i++) {
		int ret = snprintf(message + sizeof(message) - rem, rem, " %s", argv[i]);
		if (ret <= 0)
			return message;
		rem -= ret;
	}

	return message;
}

void
do_say(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char *message = argscat(argc, argv);
	OBJ player;

	nd_writef(player_ref, "You say:%s.\n", message);
	corm_get_copy(obj_hd, &player_ref, &(player));
	nd_owritef(player_ref, "%s says:%s\n", player.name, message);
	eng_nd_flush(player_ref);
}

void
do_pose(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	char *message = argscat(argc, argv);
	OBJ player;

	nd_writef(player_ref, "You %s\n", message);
	corm_get_copy(obj_hd, &player_ref, &(player));
	nd_owritef(player_ref, "%s%s\n", player.name, message);
	eng_nd_flush(player_ref);
}

void
do_wall(int fd, int argc, char *argv[])
{
	unsigned player_ref = eng_fd_player(fd);
	unsigned oi_ref;
	uint64_t sel_id = 0;
	uint8_t sel_plen = ST_PLEN_ROOT;
	int msg_start = 1;
	OBJ player;
	char buf[BUFFER_LEN];
	char *message;

	/* CMD_REGION.md §5: one selector dialect for all seven region commands --
	 * a bare world number or the keyword `cosmos` -- replacing this command's
	 * private `all` / `world <n>` keywords. The old keywords existed for a real
	 * reason, recorded in NO_WIZ.md §5: wall_message() consumes argv[start..],
	 * so `wall hello 3` has to be ONE message and a bare positional selector
	 * would make the selector and the first word of the payload the same token.
	 *
	 * The resolution: a leading token is a selector ONLY if it parses as one AND
	 * something follows it. That is what requiring a non-empty argv[2] buys --
	 * `wall 3` alone is the message "3", the only reading that does not
	 * degenerate into a usage error, and S9 leg 9 pins it.
	 *
	 * The "something follows" test is on the STRING, never on argc: axil
	 * delivers a bare verb with argc >= 2 and an empty argv[1] (NO_WIZ.md §13.2),
	 * and the trailing blank line the harness sends inflates argc further, so
	 * `argc > 2` is true for `wall 3` with argv[2] == "". Measured: the argc
	 * form printed "Usage: wall" for `wall 3` and S9 leg 9 caught it.
	 *
	 * Accepted misparse, inherent to a command whose first argument is also its
	 * first word: `wall cosmos is down` reads as selector `cosmos` + message "is
	 * down", and `wall 3 blind mice is down` likewise. Requiring a keyword
	 * prefix always would be exactly the private dialect this removes. Noted in
	 * man-src/wall.10.
	 *
	 * No shifted argv: the previous `st_cmd_region(argc - 1, argv + 1, 1, ...)`
	 * frame is gone, so `world_arg` means one thing across all seven commands. */
	if (argv[1] && *argv[1] && argv[2] && *argv[2]
			&& st_cmd_world(argv[1], &sel_id, &sel_plen) == XY_OK) {
		msg_start = 2;
	} else if (st_target_or_position(player_ref, &sel_id, &sel_plen)
			!= XY_OK) {
		st_refuse_region(player_ref, 0, ST_PLEN_ROOT);
		return;
	}

	if (msg_start >= argc || !argv[msg_start] || !*argv[msg_start]) {
		nd_writef(player_ref, "Usage: wall [region] <message>\n");
		eng_nd_flush(player_ref);
		return;
	}
	if (!st_can_region(player_ref, sel_id, sel_plen)) {
		st_refuse_region(player_ref, sel_id, sel_plen);
		return;
	}

	message = wall_message(argc, argv, msg_start);
	corm_get_copy(obj_hd, &player_ref, &(player));
	/* Trailing newline, like say and pose: without one the message has no
	 * line terminator on the wire, and a reader polling with a timeout
	 * (read -t) discards the unterminated tail -- the delivery happened but
	 * nobody polling for it could ever match it. */
	snprintf(buf, sizeof(buf), "%s shouts: %s\n", player.name, message);
	unsigned c = corm_iter(obj_hd, NULL, 0);
	OBJ oi;
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		uint64_t rid;
		uint8_t rplen;

		oi_ref = *(const unsigned *)kp;
		oi = *(const OBJ *)vp;
		if (oi.type != TYPE_ENTITY)
			continue;
		if (st_region_of_obj(oi_ref, &rid, &rplen) != 0)
			continue;
		/* Objects are visited once, so each selected recipient is hit
		 * once. There is no regions-times-objects cross product to
		 * deduplicate. */
		if (!st_region_covers(sel_id, sel_plen, rid, rplen))
			continue;
		nd_writef(oi_ref, buf);
		eng_nd_flush(oi_ref);
	}
	corm_fin(c);
}
