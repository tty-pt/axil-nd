#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/corm.h>
#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <nd/law-types.h>

void nd_io_reset(unsigned fd);

static int nd_tenant_cmds_registered;

int
nd_is_unmodded(void)
{
	return nd_tenant_cmds_registered == 0;
}

XY_IMPL(int, nd_register, char *, str, nd_cb_t *, cb, unsigned, flags)
{
	nd_tenant_cmds_registered++;
	axil_register(str, (axil_cb_t *)cb, (int)flags);
	return 0;
}

XY_IMPL(int, on_axil_command, socket_t, fd, char *, cmd)
{
	(void)cmd;
	nd_io_reset((unsigned)fd);
	return 0;
}
