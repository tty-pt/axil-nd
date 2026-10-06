all := libaxil-nd
SONAME-libaxil-nd := axil-nd

LDFLAGS-libaxil-nd := -L../axil-tty/lib
LDFLAGS-libaxil-nd-Darwin := -undefined dynamic_lookup

share != find ./htdocs -type f
share-dir := axil-nd

ENGINE-obj-y := \
	src/entity.o src/item.o src/look.o src/map.o src/match.o \
	src/mcp.o src/noise.o src/object.o src/spacetime.o src/speech.o \
	src/view.o src/wiz.o src/world.o src/io.o src/mods.o

libaxil-nd-obj-y := ${ENGINE-obj-y}

# Where the module-facing headers install. mk/include.mk:19-21 lists
# `ls include/$(FOLDER)` and :104-108 installs each to
# $(PREFIX)/include/$(FOLDER)/, so FOLDER is the whole mechanism: `nd/` is what
# makes `#include <nd/xy.h>` resolve from -I$(PREFIX)/include, the one -I that
# mk/portable.mk already puts on every module compile line and the same one that
# carries <ttypt/xy.h>. It used to be the default `ttypt`, which is why this
# needed a bespoke install-papi writing to $(PREFIX)/include/axil-nd/papi/ -- a
# directory on no default include path, so every module carried a private -I for
# it. Every module is an installed library now (see `mods:` below), so this is
# the only include path a module needs beyond its own.
FOLDER := nd
# FOLDER no longer creates include/ttypt, so the engine's own header has to
# name its own dir and its own install: install-dirs gets the directory,
# install-extra the file (mk/include.mk:123-125). Both must be set BEFORE the
# -include below: mk assigns install-dirs := ... with := immediately after its
# own +=, so a later append would miss the $(DESTDIR)$(PREFIX)/ substitution.
install-dirs += include/ttypt
install-extra := include/ttypt/axil-nd.h

-include ./../mk/include.mk

LDLIBS-libaxil-nd := -laxil -lcorm -lxylem -lislet -lqsys -laxil-tty

man: man/.stamp
man/.stamp: man-src/*.10
	@mkdir -p man
	@install -m 644 man-src/*.10 man/
	@touch $@
.PHONY: man

all: man

smoke: man ${ENGINE-obj-y}

CFLAGS-entity-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-item-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-look-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-map-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-match-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-mcp-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-noise-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-object-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-spacetime-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-speech-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-view-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-wiz-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-world-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-io-o := -fPIC -D_GNU_SOURCE -fcommon
CFLAGS-mods-o := -fPIC -D_GNU_SOURCE -fcommon

smoke: man ${ENGINE-obj-y}
	@echo "engine TU smoke: ok"
.PHONY: smoke

CFLAGS += -g \
	-I../axil-tty/include \
	-DAXIL_PREFIX='"$(PREFIX)"' \
	-DAXIL_HTDOCS='"$(PREFIX)/share/axil-nd/htdocs"'

check-lib:
	@test ! -e lib/libcorm.so || { echo "lib/libcorm.so must not exist: lib/ is build output" >&2; exit 1; }
	@lib=$$(LD_LIBRARY_PATH=./lib ldd lib/libaxil-nd.so | sed -n 's/.*=> \(\/[^ ]*libcorm\.so\).*/\1/p'); \
	test -n "$$lib" || { echo "libcorm unresolved" >&2; exit 1; }; \
	for s in corm_get_copy corm_del_value corm_next_copy; do \
		nm -D --defined-only "$$lib" | grep -q " T $$s$$" || { echo "missing $$s" >&2; exit 1; }; \
		echo "ok $$s"; \
	done
.PHONY: check-lib

demo: mods/demo/demo.so
# The installed xylem headers are prerequisites, not just an -I path. demo.so
# embeds XY_CTX_ABI_DESC at compile time, so bumping XY_CTX_ABI_VER in
# $(PREFIX)/include/ttypt/xy.h invalidates nothing else -- `make` sees only
# demo.c and include/nd/xy.h, both older, and reports "Nothing to be done"
# while leaving a binary from the previous ABI in place. Listing them here is
# what turns an ABI bump into an actual rebuild.
XY_HDRS := /usr/include/ttypt/xy.h /usr/include/ttypt/xy-mod.h
mods/demo/demo.so: mods/demo/demo.c include/nd/xy.h $(XY_HDRS)
	cd mods && cc -shared -fPIC -I../include -I/usr/include -o demo/demo.so demo/demo.c
.PHONY: demo

MODS_LOAD := mods.load
modnames != sed -n 's/^\([A-Za-z0-9_.\/-][A-Za-z0-9_.\/-]*\).*/\1/p' \
	${MODS_LOAD} 2>/dev/null
modnames := $(sort ${modnames})

# Names containing `/` are dev-tree checkouts; bare names are either in-tree or
# an installed library. nd_mods_load() (src/nd_xy.c) splits them the same way.
slashmods := $(foreach n,${modnames},$(if $(findstring /,${n}),${n}))
baremods  := $(filter-out ${slashmods},${modnames})
inmods    := $(foreach n,${baremods},$(if $(wildcard mods/$(n)/$(n).c),${n}))

mods: $(foreach m,${inmods},mods/${m}/${m}.so) \
	$(foreach m,${slashmods},${m}.$(SO))

# in-tree module: a bare name, so mods/<n>/<n>.so
mods/%.so: mods/%/%.c include/nd/xy.h $(XY_HDRS)
	cd mods/$* && $(CC) -shared -fPIC -I../../include -I/usr/include \
		-o $*.so $*.c

# A bare name with no module in this tree is an INSTALLED library, so there is
# nothing to build here: its soname symlink is all `make install` has to produce
# and dlopen() resolves it at boot. Deliberately not a prerequisite, so `make
# mods` does not require it to be installed first; a missing one is reported by
# nd_mods_load() and the engine still boots (mods.load says so too).

# A path-named module builds ITSELF: delegate to that directory's own Makefile
# and ask it for <stem>. This used to hand it the engine's nd-mod.mk with MOD=
# and ND_INC= set, which is how a bare main.c at the root of a sibling checkout
# became <stem>.so. nd-mod.mk is deleted now that every module is an installed
# library, so a path entry has to carry its own rule -- which is the honest
# contract, and is why nothing in the tracked mods.load uses this form any more.
# The test fixture in test.sh is the one remaining caller.
$(foreach m,${slashmods},$(m).$(SO)): FORCE
	$(MAKE) -C $(patsubst %/,%,$(dir $(basename $@))) $(notdir $@)

FORCE:
.PHONY: mods FORCE

test: all
	@./test.sh
.PHONY: test

install-data: man
	@dst=$(abspath $(DESTDIR)$(PREFIX))/share/axil-nd; \
	for d in art man; do \
		( cd $$d && find . -type d -exec mkdir -p "$$dst/$$d/{}" \; ) || exit 1; \
		( cd $$d && find . -type f ! -name '.stamp' -exec install -m 644 {} "$$dst/$$d/{}" \; ) || exit 1; \
	done
.PHONY: install-data

# nd-mod.mk used to be installed here, as the build contract a sibling module
# repo included to build against an installed engine (MODS.md §0.4). It is
# deleted: all eight modules are installed libraries now, so none of them
# includes it, and mk's `share` set is `find ./htdocs -type f` (Makefile:7)
# anyway, which could never have staged a file in the repo root.

# mk's uninstall does not list install-extra, so the engine's own header has to
# be removed by hand.
uninstall: uninstall-mods

uninstall-mods:
	rm -f $(DESTDIR)$(PREFIX)/include/ttypt/axil-nd.h
.PHONY: uninstall-mods

install: install-data
