# Experimental D3D11 staging upload overlay

`staging-upload.patch` applies to the pinned LGPL-2.1-or-later source in
`../sources/nvcuda-uu-remote-v0.8.tar.xz`. The modification retains that license.
The archive plus this readable patch are the complete corresponding source for
an experimental build. No proprietary client, account data or replacement
release binaries are included.

## Scope

The existing relay copies a synthetic CUDA array into a heap buffer, then calls
`UpdateSubresource` and `Flush`. The experiment instead maps a CPU-writable
D3D11 staging texture, copies CUDA data into its actual `RowPitch`, unmaps it,
then calls `CopyResource` and the same `Flush`. It still copies through CPU
memory; it is not zero-copy.

The default remains the existing path. The experimental build additionally
recognizes `UU_REMOTE_D3D11_UPLOAD=staging`. It uses the new path only for
single-mip, single-layer, non-MSAA textures in the listed uncompressed formats.
Failure to create the staging texture retains the existing path; a later Map
or CUDA copy failure is reported rather than hiding a partially written frame.
Async entry points synchronize the supplied CUDA stream before copying into
mapped CPU memory, including the per-thread-stream entry point. Bounds are
checked before computing destination addresses. First partial writes start
from zero, and subsequent partial writes preserve untouched contents.

## Build and test

The regular `make nvcuda-bridge` and bundled release payload are unchanged.
With the existing Wine development tools, Meson, Ninja and `patch` installed:

```bash
make nvcuda-staging-bridge
./tests/wine-nvdec-staging-upload.sh build/nvcuda-staging/wine --benchmark
```

The experimental build goes into a separate, ignored directory. The test uses
an isolated Wine prefix and the project's DXVK libraries; it does not modify
the user's UU prefix. Tests additionally require MinGW-w64, Wine, wineserver
and a real NVIDIA GPU with a functioning proprietary CUDA driver.

For a separately compiled, unmodified baseline:

```bash
./scripts/build-nvcuda-bridge.sh --output-directory /tmp/nvcuda-baseline/wine
```

To compare the separately compiled baseline in three alternating pairs:

```bash
./tests/wine-nvdec-staging-upload.sh \
    build/nvcuda-staging/wine --benchmark /tmp/nvcuda-baseline/wine
```

The fixture covers R8, RG8, R16, RG16, NV12 and P010 with nonaligned row widths
and changing frames, plus nonblocking-stream partial writes and invalid bounds.
A second D3D11 device reads back and checks every byte. The producer is fenced
with a D3D11 event query before the consumer reads: Flush submits work, but is
not itself a cross-device completion fence. This fence is in the test only.

`mean_bridge_ms` measures CUDA map/copy/unmap API wall time. `mean_ready_ms`
also includes waiting for the producer GPU event. Neither includes consumer
readback, remote encoding, network transport, presentation or input latency.
Benchmarks use 30 warm-up frames and 240 measured frames per run. Compare both
modes with the same fixture and alternate their order without other GPU tests
running. Raw results and limitations are in [benchmark.md](benchmark.md).

## Draft boundaries

The experiment is not selected by the launcher or included in release binaries.
Shipping it would require a separate payload/source/manifest update and testing
on the project's supported Wine 11.1+ runtime. The standalone relay was tested
with Wine 11.0, DXVK 3.0.2 and one NVIDIA adapter. Other GPUs, HDR, concurrent
resource access, multiple CUDA devices and complete wrapper/game sessions with
this exact port still need validation. This overlay does not change upstream's
resource locking, device selection or consumer synchronization policy.
