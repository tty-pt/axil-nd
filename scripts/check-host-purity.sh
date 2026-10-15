#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR"

echo "=== Checking axil-nd host purity and budget ==="

HOST_FILES="src/libaxil-nd.c src/http.c src/law_io.c src/law_store.c src/law_cmd.c src/law_data.c"

for f in $HOST_FILES; do
	if [ ! -f "$f" ]; then
		echo "FAIL: Required host file $f is missing"
		exit 1
	fi
done

MATCHES=$(grep -nE '\b(OBJ|ENT|SKEL|ROO|bio|icon|vtf|mcp|planet|region)\b|st_' $HOST_FILES || true)
if [ -n "$MATCHES" ]; then
	echo "FAIL: Game nouns detected in host source files:"
	echo "$MATCHES"
	exit 1
fi

TOTAL_LINES=$(wc -l $HOST_FILES | awk '/total/ {print $1}')
echo "Host files lines of code: $TOTAL_LINES"
if [ "${TOTAL_LINES:-0}" -gt 650 ]; then
	echo "FAIL: Host line count exceeds budget (limit: 650, got: $TOTAL_LINES)"
	exit 1
fi

echo "PASS: Host purity (0 game nouns) and budget (<= 650 LOC) verified."
