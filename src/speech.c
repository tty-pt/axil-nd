#include "uapi/io.h"

#include <stdio.h>

#include "config.h"
#include "player.h"
#include "uapi/entity.h"
#include "uapi/match.h"

/* Commands which involve speaking */
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
	OBJ player;
	char buf[BUFFER_LEN];
	char *message = argscat(argc, argv);

	if (!(eng_ent_get(player_ref).flags & EF_WIZARD)) {
		nd_writef(player_ref, CANTDO_MESSAGE);
		return;
	}

	corm_get_copy(obj_hd, &player_ref, &(player));
	snprintf(buf, sizeof(buf), "%s shouts: %s", player.name, message);
	unsigned c = corm_iter(obj_hd, NULL, 0);
	OBJ oi;
	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		oi_ref = *(const unsigned *)kp;
		oi = *(const OBJ *)vp;
		nd_writef(oi_ref, buf);
	}
}
