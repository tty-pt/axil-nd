#ifndef ND_MAP_CELL_H
#define ND_MAP_CELL_H

/*
 * map-cell.h — turn one rendered ASCII map cell into structured data.
 *
 * The ASCII map is drawn as ANSI-coloured text. A graphical map has to show
 * the same information, so the server parses each rendered cell (the string a
 * module's on_empty_tile produced, or a vtf glyph) into a glyph plus an ANSI
 * colour index, using exactly the folding nd_ansi_to_html applies in the
 * browser: bold + colour 0..7 selects the bright colour 8..15.
 *
 * Header-only and dependency-free so both the engine and the unit tests use it.
 */

#include <stdint.h>

/* The engine's `enum color` (BLACK, RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
 * WHITE) is not in ANSI order. nd.css cfN / cN classes are indexed by ANSI
 * colour, so convert before putting a colour on the wire. */
static inline uint8_t
nd_color_to_ansi(unsigned color)
{
	static const uint8_t t[8] = { 0, 1, 3, 2, 6, 4, 5, 7 };
	return t[color & 7];
}

/* Parse `s`, the first printable byte being the glyph. Escape sequences before
 * it set the colour. Returns 1 and fills *glyph / *fg when a visible glyph was
 * found; returns 0 (glyph 0, fg 7) for an empty or blank cell. */
static inline int
nd_map_cell_parse(const char *s, uint8_t *glyph, uint8_t *fg)
{
	int cur_fg = 7, bold = 0;

	*glyph = 0;
	*fg = 7;
	if (!s)
		return 0;

	while (*s) {
		if (*s == '\033' && s[1] == '[') {
			s += 2;
			while (*s && *s != 'm') {
				int code = 0;
				while (*s >= '0' && *s <= '9')
					code = code * 10 + (*s++ - '0');
				if (code == 0) {
					cur_fg = 7;
					bold = 0;
				} else if (code == 1) {
					bold = 1;
				} else if (code == 2 || code == 22) {
					bold = 0;
				} else if (code >= 30 && code <= 37) {
					cur_fg = code - 30;
				} else if (code == 39) {
					cur_fg = 7;
				}
				if (*s == ';')
					s++;
				else if (*s && *s != 'm' && (*s < '0' || *s > '9'))
					s++;
			}
			if (*s == 'm')
				s++;
			continue;
		}
		if (*s == ' ' || (unsigned char)*s < 0x20)
			return 0;
		*glyph = (uint8_t)*s;
		*fg = (uint8_t)((bold && cur_fg < 8) ? cur_fg + 8 : cur_fg);
		return 1;
	}
	return 0;
}

#endif /* ND_MAP_CELL_H */
