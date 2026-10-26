#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

#ifndef AXIL_PREFIX
#define AXIL_PREFIX "/usr"
#endif

XY_IMPL(int, nd_asset_dir, unsigned, kind, char *, out, size_t, cap)
{
	static const char *const envs[] = {
		"AXIL_ND_HTDOCS", "AXIL_ND_ART", "AXIL_ND_MAN", "AXIL_ND_MOTD"
	};
	static const char *const subdirs[] = {
		"htdocs", "art", "man", "motd"
	};
	if (kind > ND_ASSET_MOTD || !out || cap == 0)
		return 0;

	const char *ev = envs[kind] ? getenv(envs[kind]) : NULL;
	if (ev && *ev && access(ev, R_OK) == 0) {
		snprintf(out, cap, "%s", ev);
		return 1;
	}
	const char *datadir = getenv("AXIL_ND_DATADIR");
	if (datadir && *datadir) {
		snprintf(out, cap, "%s/%s", datadir, subdirs[kind]);
		if (access(out, R_OK) == 0)
			return 1;
	}
	snprintf(out, cap, "external/axil-nd/%s", subdirs[kind]);
	if (access(out, R_OK) == 0)
		return 1;

	snprintf(out, cap, "%s", subdirs[kind]);
	if (access(out, R_OK) == 0)
		return 1;

	snprintf(out, cap, AXIL_PREFIX "/share/axil-nd/%s", subdirs[kind]);
	if (access(out, R_OK) == 0)
		return 1;

	return 0;
}

#define ND_TABLE_MAX_FIELDS 16

static int
nd_table_open(const char *module, const char *table, FILE **out)
{
	char path[512];
	const char *ev;
	char dbdir[400];

	if ((ev = getenv("AXIL_ND_DATA")) && *ev) {
		snprintf(path, sizeof(path), "%s/%s.dsv", ev, table);
		if ((*out = fopen(path, "r")))
			return 1;
	}
	if ((ev = getenv("AXIL_ND_DB")) && *ev) {
		char *slash;
		snprintf(dbdir, sizeof(dbdir), "%s", ev);
		if ((slash = strrchr(dbdir, '/')))
			*slash = '\0';
		else
			snprintf(dbdir, sizeof(dbdir), ".");
		snprintf(path, sizeof(path), "%s/%s.dsv", dbdir, table);
		if ((*out = fopen(path, "r")))
			return 1;
	}
	snprintf(path, sizeof(path), "external/%s/data/%s.dsv", module, table);
	if ((*out = fopen(path, "r")))
		return 1;
	snprintf(path, sizeof(path), "../%s/data/%s.dsv", module, table);
	if ((*out = fopen(path, "r")))
		return 1;
	snprintf(path, sizeof(path), "data/%s/%s.dsv", module, table);
	if ((*out = fopen(path, "r")))
		return 1;
	snprintf(path, sizeof(path), AXIL_PREFIX "/share/axil-nd/%s/%s.dsv", module, table);
	if ((*out = fopen(path, "r")))
		return 1;
	return 0;
}

int
nd_table_load(const char *module, const char *table,
		nd_table_row_fn row, void *user)
{
	FILE *fp = NULL;
	char line[1024];
	int rows = 0;

	if (!module || !table || !row || !nd_table_open(module, table, &fp))
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		const char *fields[ND_TABLE_MAX_FIELDS];
		size_t n = 0;
		char *p = line, *nl;

		if ((nl = strpbrk(line, "\r\n")))
			*nl = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p || *p == '#')
			continue;
		for (;;) {
			char *bar = strchr(p, '|');
			if (n < ND_TABLE_MAX_FIELDS)
				fields[n++] = p;
			if (!bar)
				break;
			*bar = '\0';
			p = bar + 1;
		}
		if (row(fields, n, user) < 0)
			break;
		rows++;
	}
	fclose(fp);
	return rows;
}
