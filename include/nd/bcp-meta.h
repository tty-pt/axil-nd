#ifndef ND_BCP_META_H
#define ND_BCP_META_H

/*
 * bcp-meta.h — Declarative zero-arithmetic binary protocol framework.
 * Provides packed wire structures, safe cursor readers/writers,
 * and declarative schema helpers with zero manual byte pointer arithmetic.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ttypt/xy.h>
#include <nd/bcp-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------
 * 1. Packed Wire Layouts (Zero-Copy Memory-Mapped Access)
 * ------------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct {
	uint8_t magic[2]; /* '#', 'b' */
	uint8_t opcode;
} nd_bcp_hdr_t;

typedef struct {
	nd_bcp_hdr_t hdr;
	uint16_t current;
	uint16_t maximum;
} nd_bcp_gauge_wire_t;

typedef struct {
	nd_bcp_hdr_t hdr;
	uint32_t val;
} nd_bcp_u32_wire_t;

typedef struct {
	nd_bcp_hdr_t hdr;
	uint32_t ref;
	int16_t  x;
	int16_t  y;
	int16_t  z;
} nd_bcp_pos_wire_t;

#pragma pack(pop)

/* -------------------------------------------------------------
 * 2. Safe Zero-Arithmetic Cursor Reader & Writer
 * ------------------------------------------------------------- */

typedef struct {
	const uint8_t *cur;
	const uint8_t *end;
	int error;
} nd_bcp_reader_t;

typedef struct {
	uint8_t *cur;
	const uint8_t *end;
	int error;
} nd_bcp_writer_t;

static inline void nd_bcp_reader_init(nd_bcp_reader_t *r, const uint8_t *buf, size_t len)
{
	r->cur = buf;
	r->end = (buf && len > 0) ? (buf + len) : buf;
	r->error = (!buf || len == 0) ? 1 : 0;
}

static inline size_t nd_bcp_reader_rem(const nd_bcp_reader_t *r)
{
	if (!r || r->error || r->cur >= r->end)
		return 0;
	return (size_t)(r->end - r->cur);
}

static inline int nd_bcp_reader_ok(const nd_bcp_reader_t *r)
{
	return (r && !r->error);
}

static inline uint8_t nd_bcp_read_u8(nd_bcp_reader_t *r)
{
	if (!r || r->error || r->cur >= r->end) {
		if (r) r->error = 1;
		return 0;
	}
	return *r->cur++;
}

static inline uint16_t nd_bcp_read_u16_c(nd_bcp_reader_t *r)
{
	if (!r || r->error || (size_t)(r->end - r->cur) < 2) {
		if (r) r->error = 1;
		return 0;
	}
	uint16_t v = (uint16_t)r->cur[0] | ((uint16_t)r->cur[1] << 8);
	r->cur += 2;
	return v;
}

static inline uint32_t nd_bcp_read_u32_c(nd_bcp_reader_t *r)
{
	if (!r || r->error || (size_t)(r->end - r->cur) < 4) {
		if (r) r->error = 1;
		return 0;
	}
	uint32_t v = (uint32_t)r->cur[0] | ((uint32_t)r->cur[1] << 8) |
	             ((uint32_t)r->cur[2] << 16) | ((uint32_t)r->cur[3] << 24);
	r->cur += 4;
	return v;
}

static inline int nd_bcp_read_str_c(nd_bcp_reader_t *r, char *dst, size_t dst_cap)
{
	if (!r || r->error || !dst || dst_cap == 0) {
		if (r) r->error = 1;
		return 0;
	}
	const uint8_t *start = r->cur;
	while (r->cur < r->end && *r->cur != '\0')
		r->cur++;

	if (r->cur >= r->end) {
		r->error = 1; /* Unterminated string */
		return 0;
	}

	size_t len = (size_t)(r->cur - start);
	size_t to_copy = (len < dst_cap - 1) ? len : (dst_cap - 1);
	memcpy(dst, start, to_copy);
	dst[to_copy] = '\0';
	r->cur++; /* Skip '\0' */
	return 1;
}

static inline void nd_bcp_writer_init(nd_bcp_writer_t *w, uint8_t *buf, size_t cap)
{
	w->cur = buf;
	w->end = (buf && cap > 0) ? (buf + cap) : buf;
	w->error = (!buf || cap == 0) ? 1 : 0;
}

static inline int nd_bcp_writer_ok(const nd_bcp_writer_t *w)
{
	return (w && !w->error);
}

static inline size_t nd_bcp_writer_len(const nd_bcp_writer_t *w, const uint8_t *base)
{
	if (!w || w->error || !base || w->cur < base)
		return 0;
	return (size_t)(w->cur - base);
}

static inline void nd_bcp_write_hdr(nd_bcp_writer_t *w, uint8_t opcode)
{
	if (!w || w->error || (size_t)(w->end - w->cur) < 3) {
		if (w) w->error = 1;
		return;
	}
	w->cur[0] = ND_BCP_MAGIC_0;
	w->cur[1] = ND_BCP_MAGIC_1;
	w->cur[2] = opcode;
	w->cur += 3;
}

static inline void nd_bcp_write_u8(nd_bcp_writer_t *w, uint8_t v)
{
	if (!w || w->error || w->cur >= w->end) {
		if (w) w->error = 1;
		return;
	}
	*w->cur++ = v;
}

static inline void nd_bcp_write_u16(nd_bcp_writer_t *w, uint16_t v)
{
	if (!w || w->error || (size_t)(w->end - w->cur) < 2) {
		if (w) w->error = 1;
		return;
	}
	w->cur[0] = (uint8_t)(v & 0xFF);
	w->cur[1] = (uint8_t)((v >> 8) & 0xFF);
	w->cur += 2;
}

static inline void nd_bcp_write_u32(nd_bcp_writer_t *w, uint32_t v)
{
	if (!w || w->error || (size_t)(w->end - w->cur) < 4) {
		if (w) w->error = 1;
		return;
	}
	w->cur[0] = (uint8_t)(v & 0xFF);
	w->cur[1] = (uint8_t)((v >> 8) & 0xFF);
	w->cur[2] = (uint8_t)((v >> 16) & 0xFF);
	w->cur[3] = (uint8_t)((v >> 24) & 0xFF);
	w->cur += 4;
}

static inline void nd_bcp_write_str(nd_bcp_writer_t *w, const char *s)
{
	if (!w || w->error || !s) {
		if (w) w->error = 1;
		return;
	}
	size_t len = strlen(s);
	if ((size_t)(w->end - w->cur) < len + 1) {
		w->error = 1;
		return;
	}
	memcpy(w->cur, s, len + 1);
	w->cur += (len + 1);
}

/* -------------------------------------------------------------
 * 3. Declarative High-Level Message Helpers
 * ------------------------------------------------------------- */

static inline int nd_bcp_unpack_gauge(const uint8_t *buf, size_t len, uint16_t *cur, uint16_t *max)
{
	if (!buf || len < sizeof(nd_bcp_gauge_wire_t))
		return -1;
	const nd_bcp_gauge_wire_t *w = (const nd_bcp_gauge_wire_t *)buf;
	if (w->hdr.magic[0] != ND_BCP_MAGIC_0 || w->hdr.magic[1] != ND_BCP_MAGIC_1)
		return 0;
	if (cur) *cur = w->current;
	if (max) *max = w->maximum;
	return 1;
}

/* Decode a "bar" frame (val,max) into the named gauge. Tenant modules call
 * this from their on_bcp_channel listener for the channel they own. */
static inline int
nd_bcp_decode_gauge(nd_client_state_t *st, const char *name, const uint8_t *buf, size_t len)
{
	uint16_t val = 0, max = 0;
	int rc = nd_bcp_unpack_gauge(buf, len, &val, &max);
	if (rc != 1)
		return rc;
	return nd_client_gauge_set(st, name, val, max) == 0 ? 1 : -1;
}

static inline int nd_bcp_pack_gauge(uint8_t *buf, size_t cap, uint8_t opcode, uint16_t cur, uint16_t max)
{
	if (!buf || cap < sizeof(nd_bcp_gauge_wire_t))
		return -1;
	nd_bcp_gauge_wire_t *w = (nd_bcp_gauge_wire_t *)buf;
	w->hdr.magic[0] = ND_BCP_MAGIC_0;
	w->hdr.magic[1] = ND_BCP_MAGIC_1;
	w->hdr.opcode = opcode;
	w->current = cur;
	w->maximum = max;
	return (int)sizeof(nd_bcp_gauge_wire_t);
}

static inline int nd_bcp_unpack_u32_frame(const uint8_t *buf, size_t len, uint32_t *val)
{
	if (!buf || len < sizeof(nd_bcp_u32_wire_t))
		return -1;
	const nd_bcp_u32_wire_t *w = (const nd_bcp_u32_wire_t *)buf;
	if (w->hdr.magic[0] != ND_BCP_MAGIC_0 || w->hdr.magic[1] != ND_BCP_MAGIC_1)
		return 0;
	if (val) *val = w->val;
	return 1;
}

static inline int nd_bcp_unpack_room_msg(const uint8_t *buf, size_t len, nd_room_desc_t *out)
{
	if (!buf || len < 3 || !out)
		return -1;
	if (buf[0] != ND_BCP_MAGIC_0 || buf[1] != ND_BCP_MAGIC_1 || buf[2] != ND_BCP_ROOM)
		return 0;

	nd_bcp_reader_t r;
	nd_bcp_reader_init(&r, buf + 3, len - 3);

	out->ref = nd_bcp_read_u32_c(&r);
	nd_bcp_read_str_c(&r, out->name, sizeof(out->name));
	nd_bcp_read_str_c(&r, out->desc, sizeof(out->desc));
	if (nd_bcp_reader_rem(&r) >= 4) {
		out->exits = nd_bcp_read_u32_c(&r);
	} else {
		out->exits = 0;
	}

	return nd_bcp_reader_ok(&r) ? 1 : -1;
}

static inline int nd_bcp_pack_room_msg(uint8_t *buf, size_t cap, const nd_room_desc_t *rm)
{
	if (!buf || !rm)
		return -1;
	nd_bcp_writer_t w;
	nd_bcp_writer_init(&w, buf, cap);
	nd_bcp_write_hdr(&w, ND_BCP_ROOM);
	nd_bcp_write_u32(&w, rm->ref);
	nd_bcp_write_str(&w, rm->name);
	nd_bcp_write_str(&w, rm->desc);
	nd_bcp_write_u32(&w, rm->exits);
	return nd_bcp_writer_ok(&w) ? (int)nd_bcp_writer_len(&w, buf) : -1;
}

static inline int nd_bcp_unpack_entity_msg(const uint8_t *buf, size_t len, nd_entity_desc_t *out)
{
	if (!buf || len < 3 || !out)
		return -1;
	if (buf[0] != ND_BCP_MAGIC_0 || buf[1] != ND_BCP_MAGIC_1 || buf[2] != ND_BCP_ITEM)
		return 0;

	/* Fast path: packed 128-byte binary contract */
	if (len - 3 == sizeof(nd_entity_packet_t)) {
		const nd_entity_packet_t *pkt = (const nd_entity_packet_t *)(buf + 3);
		out->ref = pkt->ref;
		out->location = pkt->location;
		out->type = pkt->type;
		out->actions = pkt->actions;
		out->fg = pkt->fg;
		out->flags = pkt->flags;
		out->dynflags = pkt->dynflags;
		out->ch = pkt->ch;
		out->side = pkt->side;
		memcpy(out->name, pkt->name, sizeof(pkt->name));
		out->name[sizeof(out->name) - 1] = '\0';
		memcpy(out->unparse, pkt->unparse, sizeof(pkt->unparse));
		out->unparse[sizeof(out->unparse) - 1] = '\0';
		memcpy(out->art, pkt->art, sizeof(pkt->art));
		out->art[sizeof(pkt->art)] = '\0'; /* out->art is 64 */
		return 1;
	}

	nd_bcp_reader_t r;
	nd_bcp_reader_init(&r, buf + 3, len - 3);

	out->ref = nd_bcp_read_u32_c(&r);
	out->location = nd_bcp_read_u32_c(&r);
	out->dynflags = nd_bcp_read_u8(&r);
	out->side = 0;
	out->type = nd_bcp_read_u32_c(&r);
	out->actions = nd_bcp_read_u32_c(&r);
	nd_bcp_read_str_c(&r, out->name, sizeof(out->name));
	nd_bcp_read_str_c(&r, out->unparse, sizeof(out->unparse));

	if (nd_bcp_reader_rem(&r) >= 5) {
		out->fg = nd_bcp_read_u16_c(&r);
		out->flags = nd_bcp_read_u16_c(&r);
		out->ch = (char)nd_bcp_read_u8(&r);
	}
	if (nd_bcp_reader_rem(&r) > 0) {
		nd_bcp_read_str_c(&r, out->art, sizeof(out->art));
	}

	return nd_bcp_reader_ok(&r) ? 1 : -1;
}

/* -------------------------------------------------------------
 * 4. Tier 2 Declarative Macro Schema DSL (Compound Messages)
 * ------------------------------------------------------------- */

#define _ND_DECL_U8(n)        uint8_t n;
#define _ND_DECL_U16(n)       uint16_t n;
#define _ND_DECL_U32(n)       uint32_t n;
#define _ND_DECL_STR(n, cap)  char n[cap];

#define _ND_READ_U8(n)        out->n = nd_bcp_read_u8(r);
#define _ND_READ_U16(n)       out->n = nd_bcp_read_u16_c(r);
#define _ND_READ_U32(n)       out->n = nd_bcp_read_u32_c(r);
#define _ND_READ_STR(n, cap)  nd_bcp_read_str_c(r, out->n, sizeof(out->n));

#define _ND_WRITE_U8(n)       nd_bcp_write_u8(w, in->n);
#define _ND_WRITE_U16(n)      nd_bcp_write_u16(w, in->n);
#define _ND_WRITE_U32(n)      nd_bcp_write_u32(w, in->n);
#define _ND_WRITE_STR(n, cap) nd_bcp_write_str(w, in->n);

#define ND_BCP_DEFINE_SCHEMA(name, opcode, FIELDS) \
typedef struct { \
	FIELDS(_ND_DECL_U8, _ND_DECL_U16, _ND_DECL_U32, _ND_DECL_STR) \
} nd_bcp_msg_##name##_t; \
\
XY_DECL(int, on_nd_bcp_##name, const nd_bcp_msg_##name##_t *, msg); \
\
static inline int nd_bcp_pack_msg_##name(uint8_t *buf, size_t cap, const nd_bcp_msg_##name##_t *in) \
{ \
	if (!buf || !in) return -1; \
	nd_bcp_writer_t writer; \
	nd_bcp_writer_t *w = &writer; \
	nd_bcp_writer_init(w, buf, cap); \
	nd_bcp_write_hdr(w, (opcode)); \
	FIELDS(_ND_WRITE_U8, _ND_WRITE_U16, _ND_WRITE_U32, _ND_WRITE_STR) \
	return nd_bcp_writer_ok(w) ? (int)nd_bcp_writer_len(w, buf) : -1; \
} \
\
static inline int nd_bcp_unpack_msg_##name(const uint8_t *buf, size_t len, nd_bcp_msg_##name##_t *out) \
{ \
	if (!buf || len < 3 || !out) return -1; \
	if (buf[0] != ND_BCP_MAGIC_0 || buf[1] != ND_BCP_MAGIC_1 || buf[2] != (opcode)) return 0; \
	nd_bcp_reader_t reader; \
	nd_bcp_reader_t *r = &reader; \
	nd_bcp_reader_init(r, buf + 3, len - 3); \
	FIELDS(_ND_READ_U8, _ND_READ_U16, _ND_READ_U32, _ND_READ_STR) \
	return nd_bcp_reader_ok(r) ? 1 : -1; \
}

/* Standard compound schemas */
#define ND_BCP_MSG_ROOM_FIELDS(F8, F16, F32, FSTR) \
	F32(ref) \
	F32(exits) \
	FSTR(name, 64) \
	FSTR(desc, 512)

ND_BCP_DEFINE_SCHEMA(room_wire, ND_BCP_ROOM, ND_BCP_MSG_ROOM_FIELDS)

#ifdef __cplusplus
}
#endif

#endif /* ND_BCP_META_H */
