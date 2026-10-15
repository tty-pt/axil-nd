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
