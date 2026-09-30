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
mods/demo/demo.so: mods/demo/demo.c include/papi/nd-xy.h
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
mods/%.so: mods/%/%.c include/papi/nd-xy.h
	cd mods/$* && $(CC) -shared -fPIC -I../../include -I/usr/include \
		-o $*.so $*.c

# A bare name with no module in this tree is an INSTALLED library, so there is
# nothing to build here: its soname symlink is all `make install` has to produce
# and dlopen() resolves it at boot. Deliberately not a prerequisite, so `make
# mods` does not require it to be installed first; a missing one is reported by
# nd_mods_load() and the engine still boots (mods.load says so too).

$(foreach m,${slashmods},$(m).$(SO)): FORCE
	$(MAKE) -f $(CURDIR)/nd-mod.mk MOD=$(notdir $(basename $@)) \
		ND_INC=$(CURDIR)/include -C $(patsubst %/,%,$(dir $(basename $@))) \
		$(notdir $@)

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

ND_PAPI := nd-hd.h nd-xy-types.h nd-xy.h nd-hooks.h

install-papi:
	@dst=$(DESTDIR)$(PREFIX)/include/axil-nd/papi; \
	install -d "$$dst" || exit 1; \
	for h in $(ND_PAPI); do \
		install -m 644 include/papi/$$h "$$dst/$$h" || exit 1; \
	done; \
	install -d $(DESTDIR)$(PREFIX)/share/axil-nd || exit 1; \
	install -m 644 nd-mod.mk $(DESTDIR)$(PREFIX)/share/axil-nd/nd-mod.mk
.PHONY: install-papi

install: install-data install-papi
