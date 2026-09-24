all := libaxil-nd
SONAME-libaxil-nd := axil-nd

LDLIBS-libaxil-nd := -laxil -lcorm -lxylem
LDFLAGS-libaxil-nd-Darwin := -undefined dynamic_lookup

share != find ./htdocs -type f
share-dir := axil-nd

-include ./../mk/include.mk

CFLAGS += -g \
	-DAXIL_PREFIX='"$(PREFIX)"' \
	-DAXIL_HTDOCS='"$(PREFIX)/share/axil-nd/htdocs"'