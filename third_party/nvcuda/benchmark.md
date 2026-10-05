# Single-GPU upload experiment

Measured on 2026-10-05: RTX 2080 Ti, NVIDIA 595.91.07, Ryzen 7 5800X,
Ubuntu 26.04.1 GNOME Wayland/XWayland, Wine 11.0 and DXVK 3.0.2.
Both relays were built from the pinned v0.8 archive with the same Meson release
toolchain; the baseline is the **unmodified archive**, not a previous local
prototype. Exact source and binary hashes and all twelve run summaries are in
[benchmark-results.json](benchmark-results.json).

The same fixture alternated baseline / staging / staging / baseline / baseline /
staging in two independent series. Each run verified a changing 3456x1440 R8
texture on a second D3D11 device, excluded 30 warm-up frames and measured 240
frames. Other GPU tests and the offline suite had finished before these series.

| Series | Original bridge ms | Staging bridge ms | Reduction | Original GPU-ready ms | Staging GPU-ready ms | Reduction |
|---|---:|---:|---:|---:|---:|---:|
| A | 1.1317 | 0.6627 | 41.4% | 1.7293 | 1.2538 | 27.5% |
| B | 1.0603 | 0.6729 | 36.5% | 1.6446 | 1.2734 | 22.6% |

These are averages of three run means per mode. They are upload measurements
for one plane; they are **not** FPS gains or end-to-end remote/input latency
measurements. GPU-ready time includes a producer event query but excludes
consumer readback. The fixture is a controlled upload/readback workload, not
a decoded video stream or a game. Clock state and ordinary desktop background
activity were not pinned; no statistical significance or cross-GPU claim is
made. An earlier local prototype's different baseline is deliberately excluded.

Reproduce after building both versions (see [README.md](README.md)):

```bash
./tests/wine-nvdec-staging-upload.sh \
    build/nvcuda-staging/wine --benchmark /tmp/nvcuda-baseline/wine
```

The GPU regression matrix also passed: R8/RG8/R16/RG16/NV12/P010, 120 changing
frames each in both paths, plus 120 nonblocking-stream partial-write frames
for R8 and P010 with initial-zero, untouched-byte and invalid-bound checks.
The new source overlay applies without fuzz and the fixture compiles with
`-Wall -Wextra -Werror`. Both relays build successfully; the experimental
build reports no compiler warnings.

`make release-assets`, Debian metadata inspection and release checksum
verification passed. `make test` on this host completed with 127 passing checks
and 10 failures: one GTK/AppIndicator3 dependency check and nine PE-format
checks whose literal `file` output pattern differs on Ubuntu 26.04 (for example
`PE32+ executable for MS Windows 5.02 (DLL)` instead of the expected
`PE32+ executable (DLL)`). The affected payloads and tray test match upstream
unchanged; these are not claimed as passing checks. Shellcheck was unavailable
and the Wine 11.1+ focus-hook test was skipped.

The complete wrapper/session with this exact port has not been validated on
the project's supported Ubuntu 24.04 / Wine 11.1+ environment. This remains a
source-only, opt-in draft experiment with unchanged release payloads.
