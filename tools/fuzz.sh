#!/bin/sh
# Fuzzes every parser of what reaches the hub from outside (test/fuzz/fuzz_parsers.cpp) with libFuzzer, AddressSanitizer
# and UndefinedBehaviorSanitizer, for FUZZ_SECONDS (120 unless set). Needs clang on Linux or macOS, as CI has. A fault
# stops it, prints the input that caused it, and keeps it in .pio/fuzz/ to run again: .pio/fuzz/fuzz_parsers <file>.
set -e
cd "$(dirname "$0")/.."
out=.pio/fuzz
mkdir -p "$out/corpus"
python3 tools/fuzz_seeds.py "$out/corpus"
clang++ -std=gnu++17 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DCAM_TEST_HOOKS \
  -Ilib/sensorcam -Ilib/s2o_proto -Iinclude test/fuzz/fuzz_parsers.cpp lib/sensorcam/*.cpp lib/s2o_proto/*.cpp \
  -o "$out/fuzz_parsers"
"$out/fuzz_parsers" -max_total_time="${FUZZ_SECONDS:-120}" -max_len=2048 -print_final_stats=1 -artifact_prefix="$out/" "$out/corpus"
