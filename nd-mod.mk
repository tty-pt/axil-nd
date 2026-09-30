# nd-mod.mk — build rules for a standalone axil-nd game module (MODS.md §0.4).
#
# Installed to $(PREFIX)/share/axil-nd/nd-mod.mk. A sibling module repo
# (~/axil-nd-<name>) includes it and nothing else:
#
#   PREFIX ?= /usr
#   include /usr/share/axil-nd/nd-mod.mk
#
# It expects the module to be laid out as `main.c` (or any set of *.c in the
# top level) plus an optional private header dir. Keep private types in
# `include/<mod>/`, NOT `include/uapi/`: `uapi` belongs to the engine and is
# already on the include path via ND_INC, so a module putting its own uapi/
# there produces two `uapi` roots and the same nd/xy-types.h vs uapi collision
# MODS.md §3 exists to prevent. Override anything below per module.
#
# Why a shared file rather than each module carrying a copy: the include path,
# the XY headers and the `-shared -fPIC` shape are all engine contracts. A
# copy per module drifts the moment the engine moves a header -- which is
# exactly what the nd/ vs uapi/ split in MODS.md §3 makes easy to get wrong.
#
# The module is deliberately NOT linked against libaxil-nd: at runtime it is
# dlopen'd by an axil that already has the engine loaded, and XY_DECL/XY_IMPL
# resolve through the injected xy context (xy-mod.h), not through link-time
# symbols. So the only include path needed is the header tree.
#
# NOTE the artifact name is the module stem WITHOUT `.so`, because the engine's
# nd_mods_load() names that same stem in mods.load and xy_load() appends the
# suffix itself. See the mods.load header comment.

PREFIX ?= /usr
SO := so

# Locate ourselves, so a module repo works against a source checkout AND an
# installed engine with no configuration. $(lastword $(MAKEFILE_LIST)) is the
# file being read right now, i.e. this one; the dir it sits in is either the
# axil-nd source root (which has include/nd/ beside it) or
# $(PREFIX)/share/axil-nd (which does not).
#
# This replaces an earlier version that hardcoded
# `ND_INC ?= $(PREFIX)/include/axil-nd`: on a dev host with nothing installed
# that resolved to a directory that does not exist, and every sibling module
# died with `fatal error: papi/nd-xy.h: No such file or directory`. The module
# has no way to guess an unrelated repo's path, so the file that knows is the
# right place to decide.
nd-mod-dir := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

# ND_INC: the nd/ + uapi/ headers, the nd/ ones being what a module actually
# includes directly (MODS.md §3). Against an installed engine this is the same
# directory as XY_INC below -- `make install` puts include/nd/ in
# $(PREFIX)/include, so one -I now carries the game API and the XY headers
# together, and a module needs no path of its own for either.
ND_INC ?= $(if $(wildcard $(nd-mod-dir)/include/nd/xy.h),\
	$(nd-mod-dir)/include,$(PREFIX)/include)

# XY_INC: <ttypt/xy.h> and <ttypt/xy-mod.h> from libxylem, installed under the
# same prefix as the engine. No source-tree fallback: libxylem is a separate
# repo, and guessing at it here would be the same bug one level down.
XY_INC ?= $(PREFIX)/include

# Fail loudly rather than emitting a confusing "No such file" from the first
# module TU: both header trees are contracts, and a missing one means the
# engine and module were built against different worlds.
ifeq ($(wildcard $(ND_INC)/nd/xy.h),)
$(error nd/xy.h not found: ND_INC=$(ND_INC) has no nd/xy.h -- build \
against an axil-nd checkout (include nd-mod.mk from its root) or install one)
endif
ifeq ($(wildcard $(XY_INC)/ttypt/xy.h),)
$(error xy.h not found: XY_INC=$(XY_INC) has no ttypt/xy.h -- libxylem \
headers missing; set XY_INC= if they live elsewhere)
endif

# Default to the module stem from the repo name: a sibling checkout is
# ~/axil-nd-<name> (MODS.md §4) and the engine loads `../axil-nd-<name>/<name>`,
# so the artifact has to be <name>.so, not axil-nd-<name>.so. A differently
# named directory sets MOD := <name> explicitly.
MOD ?= $(patsubst axil-nd-%,%,$(notdir $(CURDIR)))
mod := $(MOD)

CFLAGS ?= -g -Wall -Wextra -Wpedantic
CFLAGS += -fPIC -I$(ND_INC) -I$(XY_INC) -Iinclude

# --no-execute-only is ARM-only and rejected by GNU ld on x86_64 (see the
# demo module's build comment), so it is not passed here.
LDFLAGS ?=

srcs := $(wildcard *.c)
objs := $(srcs:.c=.o)
target := $(mod).$(SO)

.PHONY: all clean
all: $(target)

$(target): $(objs)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(objs) $(target)
