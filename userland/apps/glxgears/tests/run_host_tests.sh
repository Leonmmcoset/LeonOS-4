#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/leonos-glxgears-tests.XXXXXX")
generated_dir=${GLXGEARS_GENERATED_DIR:-"$repo_root/build/generated/glxgears"}
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

compile_test() {
    "${CC:-cc}" -std=c11 -O2 -ffunction-sections -fdata-sections \
        -I"$repo_root/include" -I"$repo_root/third_party/portablegl" \
        -I"$repo_root/userland/apps/glxgears" \
        -I"$generated_dir" \
        -idirafter "$repo_root/userland/libc/include" \
        "$repo_root/userland/apps/glxgears/tests/$1.c" \
        -Wl,--gc-sections -lm -o "$test_dir/$1"
}

# build.py generates the upstream copy when building app:glxgears.
test -f "$generated_dir/gears-upstream.c"
compile_test gpu_backend_test
compile_test frontend_test
"$test_dir/gpu_backend_test"
for mode in 0 1 2 3 4 5 6 7 8; do
    GLXGEARS_TEST_MODE=$mode "$test_dir/frontend_test"
done
