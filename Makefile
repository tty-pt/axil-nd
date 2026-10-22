all := libaxil-nd
SONAME-libaxil-nd := axil-nd

LDFLAGS-libaxil-nd-Darwin := -undefined dynamic_lookup

share != find ./htdocs -maxdepth 1 -type f
share-dir := axil-nd

ENGINE-obj-y :=

libaxil-nd-obj-y := src/http.o src/law_io.o src/law_store.o src/law_cmd.o src/law_data.o

# Where the module-facing headers install. mk/include.mk:19-21 lists
# `ls include/$(FOLDER)` and :104-108 installs each to
# $(PREFIX)/include/$(FOLDER)/, so FOLDER is the whole mechanism: `nd/` is what
# makes `#include <nd/xy.h>` resolve from -I$(PREFIX)/include, the one -I that
# mk/portable.mk already puts on every module compile line and the same one that
# carries <ttypt/xy.h>. It used to be the default `ttypt`, which is why this
# needed a bespoke install-papi writing to $(PREFIX)/include/axil-nd/papi/ -- a
# directory on no default include path, so every module carried a private -I for
# it. Every module is an installed library now, so this is
# the only include path a module needs beyond its own.
FOLDER := nd
add-prefix-glob := ../nd-*
CLEAN_EXTRA := art art/.stamp man man/.stamp

-include ./../mk/include.mk

LDLIBS-libaxil-nd := -laxil -lcorm -lxylem -lbud

MAN_SRCS := $(wildcard $(CURDIR)/../nd-*/man-src/*.10 $(CURDIR)/../axil-*/man-src/*.10)
man: man/.stamp
man/.stamp: $(MAN_SRCS)
	@mkdir -p man
	@if [ -n "$(MAN_SRCS)" ]; then install -m 644 $(MAN_SRCS) man/; fi
	@touch $@
.PHONY: man

art: art/.stamp
art/.stamp: $(wildcard $(CURDIR)/../nd-*/art/*)
	@mkdir -p art
	@for d in $(wildcard $(CURDIR)/../nd-*/art); do \
		[ -d "$$d" ] && cp -R "$$d/." art/; \
	done
	@touch $@
.PHONY: art

all: man art

smoke: man ${ENGINE-obj-y}

CFLAGS-install-o := -fPIC -D_GNU_SOURCE
CFLAGS-http-o := -fPIC -D_GNU_SOURCE
CFLAGS-law_io-o := -fPIC -D_GNU_SOURCE
CFLAGS-law_store-o := -fPIC -D_GNU_SOURCE
CFLAGS-law_cmd-o := -fPIC -D_GNU_SOURCE
CFLAGS-law_data-o := -fPIC -D_GNU_SOURCE

smoke: man ${ENGINE-obj-y}
	@echo "engine TU smoke: ok"
.PHONY: smoke

CFLAGS += -g \
	-I../axil-tty/include \
	-I../libbud/include \
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
# demo is the engine's own in-tree module: built here, then granted to a
# region with `loadmod libnd-demo`, like every other module. There is no
# module list and no `mods:` build-list target.
# The installed xylem headers are prerequisites, not just an -I path. demo.so
# embeds XY_CTX_ABI_DESC at compile time, so bumping XY_CTX_ABI_VER in
# $(PREFIX)/include/ttypt/xy.h invalidates nothing else -- `make` sees only
# demo.c and include/nd/xy.h, both older, and reports "Nothing to be done"
# while leaving a binary from the previous ABI in place. Listing them here is
# what turns an ABI bump into an actual rebuild.
XY_HDRS := /usr/include/ttypt/xy.h /usr/include/ttypt/xy-mod.h
ND_MOD_INCLUDES := $(patsubst %,-I%,$(wildcard $(CURDIR)/../nd-*/include))
mods/demo/demo.so: mods/demo/demo.c ../nd-world/include/nd/xy.h $(XY_HDRS)
	cd mods && cc -shared -fPIC -I../include $(ND_MOD_INCLUDES) -I/usr/include -o demo/demo.so demo/demo.c
.PHONY: demo

test: all
	@./test.sh
.PHONY: test

install-data: man art
	@dst=$(abspath $(DESTDIR)$(PREFIX))/share/axil-nd; \
	for d in art man; do \
		( cd $$d && find . -type d -exec mkdir -p "$$dst/$$d/{}" \; ) || exit 1; \
		( cd $$d && find . -type f ! -name '.stamp' -exec install -m 644 {} "$$dst/$$d/{}" \; ) || exit 1; \
	done; \
	install -m 644 motd "$$dst/motd"
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
