#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

archive="$project_root/third_party/sources/nvcuda-uu-remote-v0.8.tar.xz"
printf '%s  %s\n' \
    dce82cfceceea34546119742f914d3306a3cf794fd710c640c5f4e0a5e73ed33 \
    "$archive" | sha256sum --check --status
tar -xf "$archive" -C "$test_root"
patch --batch --fuzz=0 -p1 -d "$test_root/nvcuda-uu-remote-v0.8" \
    < "$project_root/third_party/nvcuda/staging-upload.patch"
bash -n "$project_root/tests/wine-nvdec-staging-upload.sh"
if command -v x86_64-w64-mingw32-gcc >/dev/null; then
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
        -I"$test_root/nvcuda-uu-remote-v0.8/include" \
        "$project_root/tests/fixtures/nvdec-d3d11-upload-probe.c" \
        -o "$test_root/upload-probe.exe" -ld3d11 -ldxgi -luuid
else
    printf 'SKIP upload probe compilation (MinGW compiler unavailable)\n'
fi
