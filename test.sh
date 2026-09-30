#!/usr/bin/env bash
set -e

case "$(uname -s)" in
	Darwin) export DYLD_LIBRARY_PATH=./lib:${DYLD_LIBRARY_PATH} ;;
	*)      export LD_LIBRARY_PATH=./lib:${LD_LIBRARY_PATH} ;;
esac

# man/ is generated from the tracked man-src/*.10 (see Makefile). Always refresh
# rather than only when absent: a stale or partial man/ would otherwise let the
# `help` assertions below pass without the pages being there.
make --no-print-directory man

# Build the ENGINE too, not just the data. The suite boots whatever
# lib/libaxil-nd.so is on disk, so an engine edit that was never compiled was
# silently untested -- which is the same class of bug as the stale-object link
# in MODS.md §2.1, one level up. Measured here: with the path-aware branch in
# src/nd_xy.c deliberately reverted, `./test.sh` still printed "axil-nd ok"
# because only the modules were rebuilt. The engine has to be part of the
# suite's inputs.
make --no-print-directory

port=$((20000 + RANDOM % 8000))
tmpout=$(mktemp)
tmpdb=$(mktemp -d)
persist_pid_a=
persist_pid_b=
# mods.load is tracked, so the fixture below registers the test module in it
# for this run and the trap puts the shipped list back. The trap is installed
# BEFORE anything can fail -- an earlier version created the fixture and ran
# `make mods` first, so a build error left the fixture named in a tracked file
# and every later run inherited a module that did not exist.
mods_load_saved=$(mktemp)
cp mods.load "$mods_load_saved"
# Restores the tracked mods.load even on FAILURE or interrupt: a suite that
# leaves a test fixture committed in the shipped module list is worse than one
# that fails to clean up its temp dir.
trap 'cp "$mods_load_saved" mods.load; rm -f "$mods_load_saved" "$tmpout"; rm -rf "$tmpdb"; kill -9 ${mux_pid:+$mux_pid} ${tty_cat_pid:+$tty_cat_pid} ${persist_pid_a:+$persist_pid_a} ${persist_pid_b:+$persist_pid_b} 2>/dev/null || true' EXIT

# ---------------------------------------------------------------------------
# MODS.md §0.4 out-of-tree module fixture.
#
# The engine only ever had modules inside its own tree, so nothing has ever
# exercised the sibling-repo layout that Phase 1 depends on: a separate repo,
# its own Makefile, built through the shared nd-mod.mk against the papi
# headers, named in mods.load by its own path. That is the single riskiest new
# seam in 0.4 -- a path that is silently reshaped to `mods/<n>/<n>` fails with
# no error at all, just a hook that stopped firing.
#
# So build one here, in the shape Phase 1 will actually use, rather than
# trusting that a hand-made probe still works. `MOD ?= $(patsubst axil-nd-%,%)`
# in nd-mod.mk is what turns the directory name axil-nd-testprobe into the
# module name testprobe, matching ~/axil-nd-<mod> -> <mod>.
probe=../axil-nd-testprobe
rm -rf "$probe"
mkdir -p "$probe"
cat > "$probe/Makefile" <<EOF
PREFIX ?= /usr
include $(pwd)/nd-mod.mk
EOF
# Deliberately minimal: the ONLY thing this TU knows is papi/nd-xy.h and
# <ttypt/xy.h>, i.e. what an installed module gets. It includes no uapi header,
# which is the point -- that is the rule nd-xy.h documents and the demo, being
# in-tree, is not evidence for.
cat > "$probe/testprobe.c" <<'EOF'
#include <string.h>
#include <ttypt/xy-mod.h>
#include "papi/nd-xy.h"

XY_MODULE_API void xy_install(void)
{
	WARN("testprobe module installed from '%s'\n", xy.module_path);
}

XY_IMPL(int, on_enter, unsigned, player_ref, unsigned, loc_ref)
{
	unsigned h = nd_open("testprobe_ent", "u", "u", 0);
	OBJ obj;
	int rc;

	(void)loc_ref;
	memset(&obj, 0, sizeof(obj));
	rc = nd_get(HD_OBJ, &obj, &player_ref);
	nd_printf(player_ref, "[testprobe] on_enter %s hd=%s\n",
		(rc == 0 && obj.name[0]) ? obj.name : "(unresolved)",
		nd_hd_is_mod(h) ? "tagged" : "UNTAGGED");
	WARN("testprobe on_enter: HD_OBJ=%s hd=%s\n",
		(rc == 0 && obj.name[0]) ? "resolved (ok)" : "NOT RESOLVED (bug)",
		nd_hd_is_mod(h) ? "tagged" : "UNTAGGED");
	return 0;
}
EOF

# Register it by path for this run only; the trap above restores mods.load.
printf '%s\n' "$probe/testprobe" >> mods.load

# Build every module named in mods.load before booting (MODS.md §0.4). The
# engine loads whatever is in that list, so a module that no longer compiles
# would otherwise be discovered as "the hook silently stopped firing" deep in
# the suite, which is the exact failure mode Phase 1 makes worse: modules move
# into sibling repos and a stale .so on disk will happily keep serving the old
# code. `make mods` also re-checks that a mods.load path entry actually
# resolves, since the path-aware nd_mods_load() now trusts it verbatim.
make --no-print-directory mods

# the real engine boot opens its store here (world_db(): AXIL_ND_DB else
# /var/nd/std.db, unwritable on dev hosts).
export AXIL_ND_DB="$tmpdb/std.db"

# `connect <name>` runs axil_auth(), which returns non-zero when getpwnam()
# misses (axil-posix.c), and nd_player_login() then returns NOTHING, skipping
# on_enter entirely. So the guest name must be a real account on this host.
# The original nd had the same guard (nd/interface.c:1036), so this is a
# fixture constraint, not a port regression.
user=$(id -un)

axil -d -A -p "$port" -m ./lib/axil-nd >/tmp/axil_test.log 2>&1 &
mux_pid=$!

# WS key and expected accept
key=$(head -c 16 /dev/urandom | base64 | tr -d '\n')
accept=$(printf '%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11' "$key" \
	| openssl dgst -sha1 -binary | base64)

# NOTE: must use printf -v, not command substitution. Command substitution
# strips trailing newlines, which would turn the trailing \r\n\r\n into
# \r\n\r and leave the request header block unterminated; axil then waits for
# more data and never answers with a 101.
printf -v ws_request 'GET /nd HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n' \
	"$port" "$key"

# Poll until axil has initialized and port is listening (same idiom as boot A/B)
tries=50
while [ $tries -gt 0 ]; do
	grep -qF "Done." /tmp/axil_test.log 2>/dev/null && nc -z 127.0.0.1 "$port" 2>/dev/null && break
	tries=$((tries - 1))
	sleep 0.05
done
[ $tries -eq 0 ] && { echo "FAIL: axil did not become ready" >&2; exit 1; }
sleep 0.05

# Now open the real connection
exec 3<>/dev/tcp/127.0.0.1/$port
printf '%s' "$ws_request" >&3

# Read HTTP response headers
resp=""
while IFS= read -r -t3 line <&3; do
	line="${line%$'\r'}"
	resp="$resp
$line"
	[ -z "$line" ] && break
done

echo "$resp" | grep -qF "101" \
	|| { echo "FAIL: no 101 in response" >&2; exit 1; }

got_accept=$(echo "$resp" | grep -i "sec-websocket-accept" \
	| sed 's/.*: *//' | tr -d '\r\n ')
[ "$got_accept" = "$accept" ] \
	|| { echo "FAIL: accept mismatch: got '$got_accept' want '$accept'" >&2; exit 1; }

# Collect WS frames until the connect-time batch is complete (bounded).
# cat drains the socket continuously; the poll stops the moment the LAST
# expected frame (on_enter) lands, so delivery timing never flakes the run.
: >"$tmpout"
stdbuf -i0 -o0 cat <&3 >"$tmpout" &
cat_pid=$!
tries=40
while [ $tries -gt 0 ]; do
	grep -qa "on_enter" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

hex=$(xxd -p "$tmpout" | tr -d '\n')
echo "$hex" | grep -qiF "fffd1f" || { echo "FAIL: IAC DO NAWS missing"   >&2; exit 1; }
# nd has no line editor and leaves ECHO to the PTY (command_pty in axil-tty),
# so no WILL ECHO is sent on initial connect before a PTY is active:
echo "$hex" | grep -qiF "fffb01" && { echo "FAIL: IAC WILL ECHO present; nd does not echo" >&2; exit 1; }
echo "$hex" | grep -qiF "fffc03" || { echo "FAIL: IAC WONT SGA missing"  >&2; exit 1; }

# A /nd connection must be telnet-negotiated exactly ONCE. axil-tty is a
# DT_NEEDED of libaxil-nd.so (readelf -d), so it is loaded in this process, and
# its on_axil_connect() sends the same negotiations -- so a second copy of
# any of them means two modules negotiated a connection neither owns.
# NOTE: this does not currently exercise axil-tty's route gate. Traced with a
# build that fprintf()s in its on_axil_connect(): loaded as a DT_NEEDED
# dependency of libaxil-nd.so, axil dispatches that hook to axil-nd only, so
# the gate is defensive here rather than load-bearing. This assertion is the
# tripwire for if hook dispatch is ever widened to cover dependencies.
for pair in 'fffd1f:IAC DO NAWS' 'fffc03:IAC WONT SGA'; do
	pat=${pair%%:*}
	name=${pair##*:}
	seen=$(printf '%s' "$hex" | grep -oiF "$pat" | wc -l)
	[ "$seen" -eq 1 ] \
		|| { echo "FAIL: expected 1 $name on /nd, got $seen -- two modules negotiated one connection" >&2; exit 1; }
done

# Gap 6: MCP/BCP serializers fire on connect.  The `#b` + iden frames target
# DF_WEBSOCKET fds only (eng_nd_wwrite, io.c) — assert on the WS capture.
# iden values from mcp.c: AUTH_SUCCESS=6, VIEW_BUFFER=2, ACTION=9, TOD=8.
# There is no BCP_HP iden in the protocol (matches original nd mcp.c) and no
# bar fires at connect — deliberately not asserted.
echo "$hex" | grep -qiF "236206" || { echo "FAIL: BCP AUTH_SUCCESS missing" >&2; exit 1; }
echo "$hex" | grep -qiF "236202" || { echo "FAIL: BCP VIEW_BUFFER missing" >&2; exit 1; }
echo "$hex" | grep -qiF "236209" || { echo "FAIL: BCP ACTIONS missing" >&2; exit 1; }
echo "$hex" | grep -qiF "236208" || { echo "FAIL: BCP TOD missing" >&2; exit 1; }

# Engine→module→engine round trips fired from on_axil_connect:
# "[demo] player " (on_demo via nd_demo_announce) and
# "[demo] on_enter " (on_enter via nd_event_announce)
echo "$hex" | grep -qiF "5b64656d6f5d20706c6179657220" \
	|| { echo "FAIL: on_demo frame missing" >&2; exit 1; }
echo "$hex" | grep -qiF "5b64656d6f5d206f6e5f656e74657220" \
	|| { echo "FAIL: on_enter frame missing" >&2; exit 1; }

# ---------------------------------------------------------------------------
# MODS.md §0.2 handle namespaces. The demo module probes all three and
# WARN()s the result to axil's stderr, which is /tmp/axil_test.log. Each of
# these was broken before 0.2 and is a crash-or-corruption tripwire, so they
# assert on the RESULT word, not just on the probe having run:
#   - nd_open used to discard corm_open's return and hand back 0, so every
#     module that saved the handle got table 0.
#   - nd_get/nd_put/nd_iter took a module-facing handle straight to corm_*, so
#     HD_OBJ (== 7) read corm table 7 instead of the object table.
#   - an untagged module handle could collide with an enum hd outright.
log=/tmp/axil_test.log

grep -qF "demo nd_open -> 0x80000000 (tagged)" "$log" \
	|| { echo "FAIL: nd_open did not return a tagged handle" >&2; exit 1; }
grep -qF "demo nd_open/nd_put/nd_get round trip: ok" "$log" \
	|| { echo "FAIL: a module's own table did not round-trip" >&2; exit 1; }
grep -qF "demo out-of-range handles: enum=missed (ok) mod=missed (ok)" "$log" \
	|| { echo "FAIL: an out-of-range handle did not miss cleanly" >&2; exit 1; }
grep -qF "resolved (ok)" "$log" \
	|| { echo "FAIL: nd_get(HD_OBJ, ...) did not resolve to the object table" >&2; exit 1; }
grep -qF "NOT RESOLVED (bug)" "$log" \
	&& { echo "FAIL: HD_OBJ did not reach the object table" >&2; exit 1; }

# MODS.md §0.3 module ergonomics. nd_printf and nd_last are the ports of
# nd_writef and sic_last; both are compiled into the demo, so these fail at
# BUILD time if either regresses, and the first two assert the runtime path.
# "[demo] nd_printf " on the wire (5b64656d6f5d206e645f7072696e7466) proves
# the formatted text reached a real player, not just a WARN.
grep -qF "demo nd_printf compiled and dispatched" "$log" \
	|| { echo "FAIL: nd_printf did not dispatch" >&2; exit 1; }
grep -qF "demo nd_last resolved" "$log" \
	|| { echo "FAIL: nd_last did not resolve against the injected xy context" >&2; exit 1; }
echo "$hex" | grep -qiF "5b64656d6f5d206e645f7072696e7466" \
	|| { echo "FAIL: nd_printf output not on the wire" >&2; exit 1; }

# ---------------------------------------------------------------------------
# MODS.md §6 Phase 1 vertical slice. Four real modules in four sibling repos,
# named in the TRACKED mods.load by path, built by `make mods` above.
#
# All of these are CONNECT-TIME hooks (on_icon, on_add, on_new_player,
# xy_install), so they are asserted on the log right after the WS handshake.
# nd-level's on_status is the exception and is asserted separately below,
# because on_status only fires when a player runs `status` (do_status,
# src/entity.c:236) -- it is not a connect-time event. The earlier version of
# this block asserted the "Level" line against the connect-time capture, which
# could never contain it; the assertion was right, its timing was not.
grep -qF "nd-core: on_icon TYPE_ROOM -> ch='-'" "$log" \
	|| { echo "FAIL: nd-core's on_icon did not fire for a room (struct return across the bus)" >&2; exit 1; }
grep -qF "nd-other: on_add first call" "$log" \
	|| { echo "FAIL: nd-other's on_add never fired" >&2; exit 1; }
grep -qF "nd-vanilla: on_new_player teleported" "$log" \
	|| { echo "FAIL: nd-vanilla's on_new_player did not fire" >&2; exit 1; }
# nd-level's table must be a TAGGED module handle (0x80000001), never a bare
# corm handle that could alias HD_*. Asserted on the real value, because
# §0.2's whole point is that the untagged case used to be silently wrong.
grep -qE "nd-level: xy_install, level_hd = 0x8[0-9a-f]+ \(tagged\)" "$log" \
	|| { echo "FAIL: nd-level's nd_open did not return a tagged handle" >&2; exit 1; }
# nd-shop: the icon CHAIN. A decorator must be registered with nd-core (the
# single owner of on_icon) rather than co-implementing on_icon, because a
# co-implementor runs but its return replaces the owner's, and xy.last() cannot
# read the previous one mid-dispatch (MODS.md §7, measured rc=-1 ran=0).
# "#1" is the first registration, i.e. the table took it.
grep -qF "nd-core: core_icon_decorate #1" "$log" \
	|| { echo "FAIL: nd-shop did not register an icon decorator with nd-core" >&2; exit 1; }
grep -qF "nd-shop: xy_install, commands shop/buy/sell" "$log" \
	|| { echo "FAIL: nd-shop's xy_install did not run" >&2; exit 1; }
# Every slice module must be present, and none may have failed to load: a
# missing module in mods.load produces no error, just silence.
for m in core other level vanilla; do
	grep -qF "nd-$m: xy_install" "$log" \
		|| { echo "FAIL: nd-$m did not install" >&2; exit 1; }
done
! grep -qF "failed to load" "$log" \
	|| { echo "FAIL: a mods.load entry failed to load" >&2; exit 1; }

# ---------------------------------------------------------------------------
# MODS.md §0.4 out-of-tree build + path-aware loader. `make mods` above is
# the build half; this is the load half.
#
# A sibling module repo (~/axil-nd-<mod>) is named in mods.load by its STEM,
# e.g. `../axil-nd-probe/probe`, and nd_mods_load() must hand that to xy_load()
# verbatim instead of reshaping it to `mods/<n>/<n>`. The .so for this
# fixture is NOT in mods/, so nothing but the path branch can load it.
#
# The suite creates the fixture itself rather than depending on a checkout
# left lying around, so this stays green on a fresh clone.
probe=../axil-nd-testprobe
probe_mod=$(grep -cE "^${probe}/" mods.load || true)
if [ "$probe_mod" -ge 1 ]; then
	[ -f "${probe}/testprobe.so" ] \
		|| { echo "FAIL: make mods did not build the out-of-tree ${probe}" >&2; exit 1; }
	grep -qF "testprobe module installed from '${probe}/testprobe'" "$log" \
		|| { echo "FAIL: out-of-tree module was not loaded from its mods.load path" >&2; exit 1; }
	# Proves the whole §0.2/§0.3 contract again from a SECOND, genuinely
	# out-of-tree TU: a module built only against the installed papi headers
	# through nd-mod.mk, with no access to the engine tree.
	grep -qF "testprobe on_enter: HD_OBJ=resolved (ok) hd=tagged" "$log" \
		|| { echo "FAIL: out-of-tree module could not resolve HD_OBJ / tag a handle" >&2; exit 1; }
else
	# Not in the list: assert the loader did NOT try to reshape it into
	# mods/<n>/<n>, which is the regression this branch guards.
	! grep -qF "testprobe module installed" "$log" \
		|| { echo "FAIL: a module loaded that is not in mods.load" >&2; exit 1; }
fi

# Real verb round-trip: "say pong" over WS dispatches through cmds[] →
# do_say → "You say: pong." (mask key = 00 00 00 00 → payload unchanged)
# frame: FIN binary, len 9, "say pong\n"
printf '\x82\x89\x00\x00\x00\x00say pong\n' >&3

# Collect the reply until "You say:" lands (bounded), same idiom as above.
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "You say:" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "You say:" "$tmpout" \
	|| { echo "FAIL: 'You say:' reply not seen" >&2; exit 1; }

# Track 2 tripwire: WS game text must be CRLF-terminated. The game writes
# bypass the PTY (no ONLCR), so a bare LF would staircase in xterm.
# do_say emits "You say:%s.\n" with argscat's leading-space join, i.e.
# "You say: pong .\r\n" on the wire after the fix, in hex:
hexsay=$(xxd -p "$tmpout" | tr -d '\n')
echo "$hexsay" | grep -qiF "596f75207361793a20706f6e67202e0d0a" \
	|| { echo "FAIL: 'You say: pong .' not CRLF-terminated on WS" >&2; exit 1; }

# ---------------------------------------------------------------------------
# MODS.md §6: nd-level's `Level` line -- the only user-visible output the whole
# Phase 1 slice produces, and the assertion that needs a command to exist.
#
# The chain it proves, end to end on a real socket: the `status` command
# dispatches through cmds[] -> do_status (src/entity.c:230) -> nd_evt_status
# (src/nd_events.c:66) -> the module hook crosses the bus as nd-level's
# XY_IMPL -> its handler reads its OWN corm table through a handle it got from
# nd_open -> nd_printf formats -> nd_write puts it on the wire. Printing a
# level number is only possible if that table read returned, so this one line
# covers on_status, nd_open, the tagged-handle namespace, the
# engine->bus->module->bus->client round trip, and nd_printf at once.
#
# "status\n" is 7 bytes, so the WS length byte is 0x87.
printf '\x82\x87\x00\x00\x00\x00status\n' >&3
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "Level" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

# "Level\t" in hex (4c 65 76 65 6c 09). The tab matters: the engine's own
# do_status writes a space-separated dump, so a bare "Level" could be matched
# by a substring of something else, while the label-plus-tab is what nd-level
# actually emits.
hexstatus=$(xxd -p "$tmpout" | tr -d '\n')
echo "$hexstatus" | grep -qiF "4c6576656c09" \
	|| { echo "FAIL: nd-level's on_status output not on the wire" >&2; exit 1; }

# ---------------------------------------------------------------------------
# NAWS routing: axil-nd's on_axil_tick must hand EVERY frame to
# axil_tty_input() first and dispatch only the unconsumed remainder. The
# window size arrives at socket-open, long before any `sh`, so gating that
# call on axil_tty_active() dropped it and left the PTY at no size.
# Send a 9-byte NAWS subnegotiation with no PTY alive yet (0x82 0x89 = FIN
# binary + MASK + len 9, mask 00 00 00 00, payload
# FF FA 1F 00 50 00 18 FF F0 = 80x24), then round-trip a command to prove the
# frame boundary was consumed exactly and the stream stayed in sync.
printf '\x82\x89\x00\x00\x00\x00\xff\xfa\x1f\x00\x50\x00\x18\xff\xf0' >&3

# send "say naws\n" (WS binary, len 9 -> 0x89)
printf '\x82\x89\x00\x00\x00\x00say naws\n' >&3
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "You say: naws" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "You say: naws" "$tmpout" \
	|| { echo "FAIL: 'You say: naws' missing -- NAWS frame desynchronised the stream" >&2; exit 1; }

# ---------------------------------------------------------------------------
# axil-tty PTY bridge: 'help begin' renders section 10 man page over PTY
# send "help begin\n" (WS binary, len 11 -> 0x8b)
printf '\x82\x8b\x00\x00\x00\x00help begin\n' >&3
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "BEGIN" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "BEGIN" "$tmpout" \
	|| { echo "FAIL: 'help begin' man page not seen from PTY" >&2; exit 1; }

# ---------------------------------------------------------------------------
# axil-tty PTY bridge: 'sh' opens a shell session over PTY
# send "sh\n" (WS binary, len 3 -> 0x83)
printf '\x82\x83\x00\x00\x00\x00sh\n' >&3

# allow child shell to fork and initialize
sleep 0.15

# send "echo ND_TTY_OK\n" (WS binary, len 15 -> 0x8f)
printf '\x82\x8f\x00\x00\x00\x00echo ND_TTY_OK\n' >&3

# wait for and assert ND_TTY_OK
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "ND_TTY_OK" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "ND_TTY_OK" "$tmpout" \
	|| { echo "FAIL: PTY shell echo ND_TTY_OK not seen" >&2; exit 1; }

# The PTY must actually carry the geometry the client negotiated. The NAWS
# frame was sent above with no PTY alive yet, so this only lands if
# axil-nd's tick fed the frame to axil_tty_input() -- recording it in
# axil-tty's mux_wsz_map -- and axil_tty_exec() seeded the new PTY from that
# map. Measured both ways: with the old axil_tty_active() gate the frame was
# never parsed and `stty size` reports "0 0".
# send "stty size\n" (WS binary, len 10 -> 0x8a)
printf '\x82\x8a\x00\x00\x00\x00stty size\n' >&3

tries=30
while [ $tries -gt 0 ]; do
	grep -qa "24 80" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "24 80" "$tmpout" \
	|| { echo "FAIL: PTY did not receive the negotiated 80x24 geometry" >&2; exit 1; }

# send "exit\n" to promptly terminate child shell process (per recorded amendment)
printf '\x82\x85\x00\x00\x00\x00exit\n' >&3
sleep 0.15
kill -9 $cat_pid 2>/dev/null || true
wait $cat_pid 2>/dev/null || true
exec 3<&-

# ---------------------------------------------------------------------------
# Static client & asset delivery under /nd (Gap 2):
# GET /nd serves index.html (200 OK, text/html)
# GET /nd/ serves index.html (200 OK, text/html)
# GET /nd/app.css serves the client stylesheet (200 OK, text/css).
# This replaces the old /nd/styles.css assertion: styles.css is now bundled
# into app.css by the Tailwind step (see src/app.css and CSS.md Phase 1).
# The body must contain both a Tailwind utility AND an nd rule, proving the
# fold-in happened.
# GET /nd/art/biome/void/1.jpeg serves background art (200 OK, image/jpeg)
# GET /nd/nonexistent_asset.xyz returns 404
# Directory traversal is blocked / rejected

http_req() {
	printf 'GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n' "$1" "$port" \
		| nc -w2 127.0.0.1 "$port" 2>/dev/null || true
}

nd_index_resp=$(http_req "/nd")
echo "$nd_index_resp" | grep -qF "200 OK" \
	|| { echo "FAIL: GET /nd not 200 OK" >&2; exit 1; }
echo "$nd_index_resp" | grep -qiF "content-type: text/html" \
	|| { echo "FAIL: GET /nd Content-Type not text/html" >&2; exit 1; }
echo "$nd_index_resp" | grep -qF "NeverDark" \
	|| { echo "FAIL: GET /nd body does not contain NeverDark" >&2; exit 1; }

nd_slash_resp=$(http_req "/nd/")
echo "$nd_slash_resp" | grep -qF "200 OK" \
	|| { echo "FAIL: GET /nd/ not 200 OK" >&2; exit 1; }
echo "$nd_slash_resp" | grep -qiF "content-type: text/html" \
	|| { echo "FAIL: GET /nd/ Content-Type not text/html" >&2; exit 1; }

nd_css_resp=$(http_req "/nd/app.css")
echo "$nd_css_resp" | grep -qF "200 OK" \
	|| { echo "FAIL: GET /nd/app.css not 200 OK" >&2; exit 1; }
echo "$nd_css_resp" | grep -qiF "content-type: text/css" \
	|| { echo "FAIL: GET /nd/app.css Content-Type not text/css" >&2; exit 1; }
nd_css_body=$(printf '%s' "$nd_css_resp" | sed '1,/^\r$/d')
echo "$nd_css_body" | grep -qF ".flex" \
	|| { echo "FAIL: /nd/app.css has no Tailwind utilities" >&2; exit 1; }
echo "$nd_css_body" | grep -qF "#right-panel" \
	|| { echo "FAIL: /nd/app.css has no NeverDark rules (styles.css not folded in)" >&2; exit 1; }

nd_art_resp=$(http_req "/nd/art/biome/void/1.jpeg" | head -n 20 | tr -d '\0')
echo "$nd_art_resp" | grep -qF "200 OK" \
	|| { echo "FAIL: GET /nd/art/biome/void/1.jpeg not 200 OK" >&2; exit 1; }
echo "$nd_art_resp" | grep -qiF "content-type: image/jpeg" \
	|| { echo "FAIL: GET /nd/art/biome/void/1.jpeg Content-Type not image/jpeg" >&2; exit 1; }

nd_404_resp=$(http_req "/nd/nonexistent_asset.xyz")
echo "$nd_404_resp" | grep -qF "404 Not Found" \
	|| { echo "FAIL: GET /nd/nonexistent_asset.xyz did not 404" >&2; exit 1; }

nd_trav_resp=$(http_req "/nd/../Makefile")
echo "$nd_trav_resp" | grep -qF "404 Not Found" \
	|| echo "$nd_trav_resp" | grep -qF "400" \
	|| [ -z "$nd_trav_resp" ] \
	|| { echo "FAIL: directory traversal not blocked" >&2; exit 1; }

# ---------------------------------------------------------------------------
# /tty is axil-tty's own route and must be vanilla: the browser terminal page
# and an auto-spawned shell, with no game anywhere on the socket. axil
# dispatches on_axil_connect to every loaded module whatever route matched, so
# libaxil-nd's hook runs on a /tty socket too; nd_tty_owned() in libaxil-nd.c is
# the only thing keeping nd_connect() off it. These are the tripwires for that
# gate: before it existed, a /tty client got a full game login (a player
# created in the store, BCP frames on the wire) and then the shell.
tty_out="$tmpdb/tty.out"

tty_page_resp=$(http_req "/tty")
echo "$tty_page_resp" | grep -qF "200 OK" \
	|| { echo "FAIL: GET /tty not 200 OK" >&2; exit 1; }
echo "$tty_page_resp" | grep -qiF "content-type: text/html" \
	|| { echo "FAIL: GET /tty Content-Type not text/html" >&2; exit 1; }
# The axil-tty page, not the NeverDark client the /nd assertions above expect.
echo "$tty_page_resp" | grep -qF "AXIL Terminal" \
	|| { echo "FAIL: GET /tty did not serve the axil-tty page" >&2; exit 1; }
echo "$tty_page_resp" | grep -qF "NeverDark" \
	&& { echo "FAIL: GET /tty served the /nd client" >&2; exit 1; }

# Same upgrade dance as /nd, on its own fd and capture file.
tty_key=$(head -c 16 /dev/urandom | base64 | tr -d '\n')
tty_accept=$(printf '%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11' "$tty_key" \
	| openssl dgst -sha1 -binary | base64)
printf -v ws_request_tty 'GET /tty HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n' \
	"$port" "$tty_key"

exec 7<>/dev/tcp/127.0.0.1/$port
printf '%s' "$ws_request_tty" >&7

tty_resp=""
while IFS= read -r -t3 line <&7; do
	line="${line%$'\r'}"
	tty_resp="$tty_resp
$line"
	[ -z "$line" ] && break
done

echo "$tty_resp" | grep -qF "101" \
	|| { echo "FAIL: no 101 for GET /tty upgrade" >&2; exit 1; }
tty_got_accept=$(echo "$tty_resp" | grep -i "sec-websocket-accept" \
	| sed 's/.*: *//' | tr -d '\r\n ')
[ "$tty_got_accept" = "$tty_accept" ] \
	|| { echo "FAIL: /tty accept mismatch: got '$tty_got_accept' want '$tty_accept'" >&2; exit 1; }

: >"$tty_out"
stdbuf -i0 -o0 cat <&7 >"$tty_out" &
tty_cat_pid=$!

# WILL ECHO is axil-tty's own first statement on its own route, so it is the
# last of the three to arrive; waiting on it means all three are in.
tries=40
while [ $tries -gt 0 ]; do
	grep -qa $'\xff\xfb\x01' "$tty_out" && break
	sleep 0.05
	tries=$((tries - 1))
done

tty_hex=$(xxd -p "$tty_out" | tr -d '\n')
# Unlike /nd, a PTY exists from this route's on_axil_connect, so ECHO is the
# server's to state and there is exactly one statement of each option.
for pair in 'fffb01:IAC WILL ECHO' 'fffd1f:IAC DO NAWS' 'fffc03:IAC WONT SGA'; do
	pat=${pair%%:*}
	name=${pair##*:}
	seen=$(printf '%s' "$tty_hex" | grep -oiF "$pat" | wc -l)
	[ "$seen" -eq 1 ] \
		|| { echo "FAIL: expected 1 $name on /tty, got $seen -- two modules negotiated one connection" >&2; exit 1; }
done

# The game must not be on this socket at all: no player was created, so none of
# the connect-time MCP/BCP idens or the module announce frames may appear.
echo "$tty_hex" | grep -qiF "2362" \
	&& { echo "FAIL: BCP frames on /tty -- the game ran on axil-tty's route" >&2; exit 1; }
grep -qa "on_enter" "$tty_out" \
	&& { echo "FAIL: on_enter on /tty -- nd_connect ran on axil-tty's route" >&2; exit 1; }

# The auto-spawned login shell: NAWS is the trigger, so no `sh` is sent here.
# Same 9-byte 80x24 subnegotiation the /nd section uses, then type at the shell.
printf '\x82\x89\x00\x00\x00\x00\xff\xfa\x1f\x00\x50\x00\x18\xff\xf0' >&7
sleep 0.3
# send "echo ND_TTY_AUTO_OK\n" (WS binary, len 19 -> 0x93)
printf '\x82\x93\x00\x00\x00\x00echo ND_TTY_AUTO_OK\n' >&7

tries=40
while [ $tries -gt 0 ]; do
	grep -qa "ND_TTY_AUTO_OK" "$tty_out" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "ND_TTY_AUTO_OK" "$tty_out" \
	|| { echo "FAIL: /tty auto-shell echo ND_TTY_AUTO_OK not seen" >&2; exit 1; }

kill -9 $tty_cat_pid 2>/dev/null || true
wait $tty_cat_pid 2>/dev/null || true
exec 7<&-

# ---------------------------------------------------------------------------
# Raw-telnet guest path: connect a guest over a plain TCP socket (no WS).
# First non-HTTP line marks the fd raw; telnet negotiation + banner are
# emitted, then `connect <name>` boots the player through the same landing
# as WS users (CF_NOAUTH passes cmd_proc's pre-auth gate).
exec 4<>/dev/tcp/127.0.0.1/$port

# Collect raw telnet session frames continuously
: >"$tmpout"
stdbuf -i0 -o0 cat <&4 >"$tmpout" &
cat_pid=$!
sleep 0.05

# axil_read() only dispatches a COMPLETE head: head_complete() accepts LFLF or
# CRLFCRLF, never a single LF or a single CRLF (libaxil.c). A one-line send is
# stashed in the descriptor and never dispatched, so every raw command below
# terminates with a blank line.
printf 'connect %s\n\n' "$user" >&4

tries=40
while [ $tries -gt 0 ]; do
	grep -qa "on_enter" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

grep -qa "on_enter" "$tmpout" \
	|| { echo "FAIL: raw guest on_enter missing" >&2; exit 1; }

hex3=$(xxd -p "$tmpout" | tr -d '\n')
echo "$hex3" | grep -qiF "fffd1f" || { echo "FAIL: raw IAC DO NAWS missing" >&2; exit 1; }
echo "$hex3" | grep -qiF "fffb01" && { echo "FAIL: raw IAC WILL ECHO present before PTY" >&2; exit 1; }
echo "$hex3" | grep -qiF "fffc03" || { echo "FAIL: raw IAC WONT SGA missing" >&2; exit 1; }

# say round-trip over the raw socket (no WS framing).
printf 'say pong\n\n' >&4
tries=30
while [ $tries -gt 0 ]; do
	grep -qa "You say:" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

# ---------------------------------------------------------------------------
# Raw-telnet live-PTY path: axil_tty_input() must get first refusal here too.
# Before that, strip_telnet() swallowed the NAWS payload and -- worse -- once a
# PTY was live the bytes were still handed to cmd_proc, so a guest typing at
# the shell had its keystrokes read as ND commands and nothing reached the PTY.
# Send raw NAWS (no WS framing), then open a shell and type at it.
printf '\xff\xfa\x1f\x00\x50\x00\x18\xff\xf0' >&4
sleep 0.05
printf 'sh\n\n' >&4
sleep 0.3
printf 'echo ND_RAW_OK\n\n' >&4

tries=40
while [ $tries -gt 0 ]; do
	grep -qa "ND_RAW_OK" "$tmpout" && break
	sleep 0.05
	tries=$((tries - 1))
done

kill -9 $cat_pid 2>/dev/null || true
wait $cat_pid 2>/dev/null || true
exec 4<&-

grep -qa "You say:" "$tmpout" \
	|| { echo "FAIL: raw 'You say:' reply not seen" >&2; exit 1; }

grep -qa "ND_RAW_OK" "$tmpout" \
	|| { echo "FAIL: raw live-PTY shell echo ND_RAW_OK not seen" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Persistence regression: the store must survive an orderly shutdown and a
# second boot must REUSE the saved player.  Boot A on a fresh store:
# `connect $user` -> `save` -> `kill -SEGV`.  Boot B on the SAME store:
# `connect $user` again must log the real player ref (not the new-player
# sentinel /4294967295) and must NOT run eng_object_add (no recreation).
# WARN and eng_object_add both go to stderr (libqsys + object.c), so each
# boot's axil stderr is captured to its own log file for the greps.
persist_db="$tmpdb/persist.db"
port_a=$((port + 1))
port_b=$((port + 2))
log_a="$tmpdb/persist-a.log"
log_b="$tmpdb/persist-b.log"

AXIL_ND_DB="$persist_db" axil -d -A -p "$port_a" -m ./lib/axil-nd >"$log_a" 2>&1 &
persist_pid_a=$!

# bounded-poll: wait for nd_world_init's "Done." (world.c) and open port before connecting
tries=50
while [ $tries -gt 0 ]; do
	grep -qF "Done." "$log_a" 2>/dev/null && nc -z 127.0.0.1 "$port_a" 2>/dev/null && break
	tries=$((tries - 1))
	sleep 0.05
done
[ $tries -eq 0 ] && { echo "FAIL: boot A did not init" >&2; exit 1; }

exec 5<>/dev/tcp/127.0.0.1/$port_a
printf 'connect %s\n\n' "$user" >&5
tries=50
while [ $tries -gt 0 ]; do
	grep -qF "nd_player_login: '$user'" "$log_a" 2>/dev/null && break
	tries=$((tries - 1))
	sleep 0.1
done
[ $tries -eq 0 ] && { echo "FAIL: boot A login not seen" >&2; exit 1; }

printf 'save\n\n' >&5
sleep 0.3
exec 5<&-

kp=$persist_pid_a
persist_pid_a=
kill -SEGV "$kp" 2>/dev/null || true
wait "$kp" 2>/dev/null || true

sz=$(stat -c %s "$persist_db" 2>/dev/null || echo 0)
[ "${sz:-0}" -gt 0 ] || { echo "FAIL: boot A store empty" >&2; exit 1; }
grep -qF "eng_object_add" "$log_a" \
	|| { echo "FAIL: boot A did not create the player" >&2; exit 1; }

AXIL_ND_DB="$persist_db" axil -d -A -p "$port_b" -m ./lib/axil-nd >"$log_b" 2>&1 &
persist_pid_b=$!

tries=50
while [ $tries -gt 0 ]; do
	grep -qF "Done." "$log_b" 2>/dev/null && nc -z 127.0.0.1 "$port_b" 2>/dev/null && break
	tries=$((tries - 1))
	sleep 0.05
done
[ $tries -eq 0 ] && { echo "FAIL: boot B did not init" >&2; exit 1; }

exec 6<>/dev/tcp/127.0.0.1/$port_b
printf 'connect %s\n\n' "$user" >&6
tries=50
while [ $tries -gt 0 ]; do
	grep -qF "nd_player_login: '$user'" "$log_b" 2>/dev/null && break
	tries=$((tries - 1))
	sleep 0.1
done
[ $tries -eq 0 ] && { echo "FAIL: boot B login not seen" >&2; exit 1; }
exec 6<&-

kp=$persist_pid_b
persist_pid_b=
kill -SEGV "$kp" 2>/dev/null || true
wait "$kp" 2>/dev/null || true

grep -qF "nd_player_login: '$user'" "$log_b" \
	|| { echo "FAIL: boot B login missing" >&2; exit 1; }
grep -qF "eng_object_add" "$log_b" \
	&& { echo "FAIL: boot B re-created the player" >&2; exit 1; }
grep -qF '/4294967295' "$log_b" \
	&& { echo "FAIL: boot B did not reuse the persisted player" >&2; exit 1; }
sz=$(stat -c %s "$persist_db" 2>/dev/null || echo 0)
[ "${sz:-0}" -gt 0 ] || { echo "FAIL: boot B store empty" >&2; exit 1; }

exec 4<&-

echo "axil-nd ok"