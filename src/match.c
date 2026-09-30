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
	unsigned match;
	if (*name == NUMBER_TOKEN) {
		match = parse_unsigned(name + 1);
		if (match < 0 || !eng_obj_exists(match))
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
	while (corm_next(&kp, &vp, c)) {
		where_ref = *(const unsigned *)kp;
		tmp_ref = *(const unsigned *)vp;
		if (tmp_ref == absolute_ref) {
			corm_fin(c);
			break;
		}

		OBJ tmp;
		corm_get_copy(obj_hd, &tmp_ref, &(tmp));
		if (string_match(tmp.name, name)) {
			if (nth <= 0) {
				corm_fin(c);
				break;
			}
			nth--;
		}
	}

	return tmp_ref;
}
