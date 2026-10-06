#include "uapi/match.h"

#include <ctype.h>
#include <stdlib.h>

#include "params.h"
#include "uapi/entity.h"
#include "player.h"

unsigned
eng_ematch_me(unsigned player_ref, char *str)
{
	if (!strcmp(str, "me"))
		return player_ref;
	else
		return NOTHING;
}

unsigned
eng_ematch_here(unsigned player_ref, char *str)
{
	if (!strcmp(str, "here")) {
		OBJ player;
		corm_get_copy(obj_hd, &player_ref, &(player));
		return player.location;
	} else
		return NOTHING;
}

unsigned
eng_ematch_mine(unsigned player_ref, char *str)
{
	return eng_ematch_at(player_ref, player_ref, str);
}

unsigned
eng_ematch_near(unsigned player_ref, char *str)
{
	OBJ player;
	corm_get_copy(obj_hd, &player_ref, &(player));
	return eng_ematch_at(player_ref, player.location, str);
}

/* all ematch
 * (not found by the linker if it is not static?)
 */
unsigned
eng_ematch_all(unsigned player_ref, char *name)
{
	unsigned res;

	if (
			(res = eng_ematch_me(player_ref, name)) != NOTHING
			|| (res = eng_ematch_here(player_ref, name)) != NOTHING
			|| (res = eng_ematch_absolute(name)) != NOTHING
			|| (res = eng_ematch_near(player_ref, name)) != NOTHING
			|| (res = eng_ematch_mine(player_ref, name)) != NOTHING
			|| (res = eng_ematch_player(name)) != NOTHING
	   )
		return res;

	else
		return NOTHING;
}


unsigned
eng_ematch_player(char *name)
{
	return player_get(name);
}

static unsigned
parse_unsigned(const char *s)
{
	const char *p;
	long x;

	x = atol(s);
	if (x > 0) {
		return x;
	} else if (x == 0) {
		/* check for 0 */
		for (p = s; *p; p++) {
			if (*p == '0')
				return 0;
			if (!isspace(*p))
				break;
		}
	}
	/* else x < 0 or s != 0 */
	return NOTHING;
}

/* returns nnn if name = #nnn, else NOTHING */
unsigned
eng_ematch_absolute(char *name)
{
	if (*name == NUMBER_TOKEN) {
		unsigned match = parse_unsigned(name + 1);

		/* parse_unsigned is unsigned and already maps a bad parse to
		 * NOTHING, so there is no `match < 0` to test here -- the old
		 * `if (match < 0 || ...)` could never be true and every guard on
		 * this path rested on eng_obj_exists alone. Presence is the whole
		 * test, and it is now asked in the right direction (see
		 * eng_obj_exists in object.c, which used to report "present" for
		 * absent rows). An absent ref must NOT reach its callers: they
		 * corm_get_copy it straight out of the store, and on a row that is
		 * not there that aborts the daemon. */
		if (!match || !eng_obj_exists(match))
			return NOTHING;
		else
			return match;
	} else
		return NOTHING;
}

static inline int
string_prefix(register const char *string, register const char *prefix)
{
	while (*string && *prefix && tolower(*string) == tolower(*prefix))
		string++, prefix++;
	return *prefix == '\0';
}

/* accepts only nonempty matches starting at the beginning of a word */
static inline const char *
string_match(register const char *src, register const char *sub)
{
	if (*sub != '\0') {
		while (*src) {
			if (string_prefix(src, sub))
				return src;
			/* else scan to beginning of next word */
			while (*src && isalnum(*src))
				src++;
			while (*src && !isalnum(*src))
				src++;
		}
	}
	return 0;
}

unsigned
eng_ematch_at(unsigned player_ref, unsigned where_ref, char *name) {
	unsigned what_ref = eng_ematch_absolute(name),
	      absolute_ref, tmp_ref = NOTHING;
	/* NOTHING is a legitimate miss here (no absolute match); it falls
	 * through to the contents scan below. Only a validated ref is copied,
	 * strictly — a dangling non-NOTHING ref means store corruption. */
	if (what_ref != NOTHING) {
		OBJ what;
		corm_get_copy(obj_hd, &what_ref, &what);
		if (what.location == where_ref)
			return what_ref;
	}

	ENT ent = eng_ent_get(player_ref);
	unsigned nth = ent.select;
	ent.select = 0;
	eng_ent_set(player_ref, &ent);

	absolute_ref = eng_ematch_absolute(name);

	if (absolute_ref != NOTHING && !eng_controls(player_ref, absolute_ref))
		absolute_ref = NOTHING;

	unsigned c = corm_iter(contents_hd, &where_ref, CM_RANGE);
	const void *kp, *vp;

	/* tmp_ref is only ever assigned on an ACTUAL match, and the iterator is
	 * always finished. Both were wrong: the loop assigned tmp_ref on every
	 * iteration and returned it unconditionally, so exhausting the scan
	 * without a match reported "the last object in the room" as a hit --
	 * which is how `teleport #<a real ref> here` silently moved the dolphin
	 * instead of the player, since eng_ematch_absolute had discarded the real
	 * ref and this scan was all that stood between the command and a bystander.
	 * It also leaked the iterator on the exhausting path (corm_fin ran only on
	 * the two break arms).
	 *
	 * where_ref is the RANGE KEY handed to corm_iter and must not be
	 * reassigned from the iteration; `where_ref = *(const unsigned *)kp`
	 * overwrote it with each row's key while corm_next was still walking it. */
	while (corm_next(&kp, &vp, c)) {
		unsigned obj_ref = *(const unsigned *)vp;
		if (absolute_ref != NOTHING && obj_ref == absolute_ref) {
			tmp_ref = obj_ref;
			break;
		}

		OBJ tmp;
		corm_get_copy(obj_hd, &obj_ref, &(tmp));
		if (string_match(tmp.name, name)) {
			if (nth <= 0) {
				tmp_ref = obj_ref;
				break;
			}
			nth--;
		}
	}
	corm_fin(c);

	return tmp_ref;
}
