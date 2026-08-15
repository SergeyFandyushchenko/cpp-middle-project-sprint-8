#!/usr/bin/env bash
set -euo pipefail

TOOL="${1:-./build/refactor_tool}"
CXX="${CXX:-clang++-20}"
REPORT_DIR="${2:-reports}"
SOURCE="tests/tests_data/leak_example.cpp"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p "$REPORT_DIR"
cp "$SOURCE" "$WORK_DIR/before.cpp"
cp "$SOURCE" "$WORK_DIR/after.cpp"

"$CXX" -std=c++23 -O0 -g -fsanitize=address -fno-omit-frame-pointer \
    "$WORK_DIR/before.cpp" -o "$WORK_DIR/before"

set +e
ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=0 \
    "$WORK_DIR/before" >"$REPORT_DIR/asan_before.txt" 2>&1
BEFORE_STATUS=$?
set -e

"$TOOL" --log-file "$REPORT_DIR/asan_refactor.log" "$WORK_DIR/after.cpp" -- -std=c++23
"$CXX" -std=c++23 -O0 -g -fsanitize=address -fno-omit-frame-pointer \
    "$WORK_DIR/after.cpp" -o "$WORK_DIR/after"

set +e
ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=0 \
    "$WORK_DIR/after" >"$REPORT_DIR/asan_after.txt" 2>&1
AFTER_STATUS=$?
set -e

{
    echo "before exit code: $BEFORE_STATUS"
    echo "after exit code:  $AFTER_STATUS"
} >"$REPORT_DIR/asan_summary.txt"

printf 'ASAN reports written to %s\n' "$REPORT_DIR"
