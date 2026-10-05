#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
relay_root=${1:-"$project_root/build/nvcuda-staging/wine"}
benchmark=${2:-}
baseline_root=${3:-"$relay_root"}
readonly relay_root benchmark baseline_root
[[ $# -le 3 && ( -z $benchmark || $benchmark == --benchmark ) ]] || exit 2
for tool in tar x86_64-w64-mingw32-gcc wine wineserver; do
    command -v "$tool" >/dev/null || { printf 'Missing test tool: %s\n' "$tool" >&2; exit 77; }
done
[[ -r $relay_root/x86_64-unix/nvcuda.dll.so && -r $relay_root/x86_64-windows/nvcuda.dll ]] || {
    printf 'Build the experimental relay first: make nvcuda-staging-bridge\n' >&2
    exit 77
}

test_root=$(mktemp -d)
export WINEPREFIX="$test_root/wineprefix"
trap 'wineserver -k; rm -rf -- "$test_root"' EXIT
mkdir -p "$test_root/app"
tar -xf "$project_root/third_party/sources/nvcuda-uu-remote-v0.8.tar.xz" -C "$test_root"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$test_root/nvcuda-uu-remote-v0.8/include" \
    "$project_root/tests/fixtures/nvdec-d3d11-upload-probe.c" \
    -o "$test_root/app/upload-probe.exe" -ld3d11 -ldxgi -luuid
for dll in d3d11.dll dxgi.dll; do
    install -m0644 "$project_root/lib/uu-remote-for-linux/hwdecode/dxvk/x64/$dll" "$test_root/app/$dll"
done
install -m0644 "$relay_root/x86_64-windows/nvcuda.dll" "$test_root/app/nvcuda.dll"
export WINEARCH=win64
export WINEDLLPATH="$(cd -- "$relay_root/x86_64-unix" && pwd -P)"
export WINEDLLOVERRIDES='d3d11=n;dxgi=n;nvcuda=b;mscoree,mshtml=;winemenubuilder.exe=d'
export WINEDEBUG=-all
export DXVK_LOG_LEVEL=none

# The second device reads every byte on every frame. The upload API timing
# excludes readback and is not a measurement of end-to-end display latency.
for mode in update staging; do
    for format in R8 RG8 R16 RG16 NV12 P010; do
        printf 'MODE %s\n' "$mode"
        UU_REMOTE_D3D11_UPLOAD=$mode wine "$test_root/app/upload-probe.exe" --format "$format"
    done
done
for format in R8 P010; do
    UU_REMOTE_D3D11_UPLOAD=staging wine "$test_root/app/upload-probe.exe" \
        --format "$format" --async --partial --check-bounds
done
if [[ $benchmark == --benchmark ]]; then
    run=0
    for mode in update staging staging update update staging; do
        run=$((run + 1))
        selected_root=$relay_root
        [[ $mode != update ]] || selected_root=$baseline_root
        install -m0644 "$selected_root/x86_64-windows/nvcuda.dll" "$test_root/app/nvcuda.dll"
        export WINEDLLPATH="$(cd -- "$selected_root/x86_64-unix" && pwd -P)"
        printf 'BENCHMARK RUN %s MODE %s\n' "$run" "$mode"
        UU_REMOTE_D3D11_UPLOAD=$mode wine "$test_root/app/upload-probe.exe" \
            --format R8 --width 3456 --height 1440 --frames 240 --warmup 30
    done
fi
