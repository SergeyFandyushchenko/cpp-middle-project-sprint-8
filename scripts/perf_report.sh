#!/usr/bin/env bash
set -euo pipefail

TOOL="${1:-./build/refactor_tool}"
CXX="${CXX:-clang++-20}"
REPORT_DIR="${2:-reports}"
SOURCE="tests/tests_data/perf_example.cpp"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

if ! command -v perf >/dev/null 2>&1; then
    echo "perf is not installed. Install the Linux perf package first." >&2
    exit 1
fi

mkdir -p "$REPORT_DIR"
cp "$SOURCE" "$WORK_DIR/before.cpp"
cp "$SOURCE" "$WORK_DIR/after.cpp"

"$TOOL" --log-file "$REPORT_DIR/perf_refactor.log" "$WORK_DIR/after.cpp" -- -std=c++23

# -O0 is intentional here: the example is meant to measure copies in range-for;
# aggressive optimization may remove the otherwise unused loop completely.
"$CXX" -std=c++23 -O0 -g "$WORK_DIR/before.cpp" -o "$WORK_DIR/before"
"$CXX" -std=c++23 -O0 -g "$WORK_DIR/after.cpp" -o "$WORK_DIR/after"

sudo perf stat -r 5 -o "$REPORT_DIR/perf_before.txt" -- "$WORK_DIR/before"
sudo perf stat -r 5 -o "$REPORT_DIR/perf_after.txt" -- "$WORK_DIR/after"

sudo perf record -q -g --call-graph dwarf -o "$WORK_DIR/perf_before.data" -- "$WORK_DIR/before"
sudo perf report --stdio -i "$WORK_DIR/perf_before.data" >"$REPORT_DIR/perf_before_report.txt"

sudo perf record -q -g --call-graph dwarf -o "$WORK_DIR/perf_after.data" -- "$WORK_DIR/after"
sudo perf report --stdio -i "$WORK_DIR/perf_after.data" >"$REPORT_DIR/perf_after_report.txt"

printf 'perf reports written to %s\n' "$REPORT_DIR"
