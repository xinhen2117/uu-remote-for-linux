#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cuda.h"

#define CU_MEMORYTYPE_HOST 1
#define CU_MEMORYTYPE_DEVICE 2
#define CU_MEMORYTYPE_ARRAY 3
#define CU_ERROR_INVALID_DEVICE 101

typedef CUresult (WINAPI *cu_init_fn)(unsigned int);
typedef CUresult (WINAPI *cu_device_get_fn)(CUdevice *, int);
typedef CUresult (WINAPI *cu_d3d11_get_device_fn)(CUdevice *, IDXGIAdapter *);
typedef CUresult (WINAPI *cu_ctx_create_fn)(CUcontext *, unsigned int, CUdevice);
typedef CUresult (WINAPI *cu_ctx_destroy_fn)(CUcontext);
typedef CUresult (WINAPI *cu_mem_alloc_fn)(CUdeviceptr *, size_t);
typedef CUresult (WINAPI *cu_mem_free_fn)(CUdeviceptr);
typedef CUresult (WINAPI *cu_memcpy_htod_fn)(CUdeviceptr, const void *, size_t);
typedef CUresult (WINAPI *cu_graphics_register_fn)(
    CUgraphicsResource *, ID3D11Resource *, unsigned int
);
typedef CUresult (WINAPI *cu_graphics_map_fn)(
    unsigned int, CUgraphicsResource *, CUstream
);
typedef CUresult (WINAPI *cu_graphics_get_array_fn)(
    CUarray *, CUgraphicsResource, unsigned int, unsigned int
);
typedef CUresult (WINAPI *cu_memcpy_2d_fn)(const CUDA_MEMCPY2D *);
typedef CUresult (WINAPI *cu_graphics_unmap_fn)(
    unsigned int, CUgraphicsResource *, CUstream
);
typedef CUresult (WINAPI *cu_graphics_unregister_fn)(CUgraphicsResource);

struct cuda_api {
    cu_init_fn init;
    cu_device_get_fn device_get;
    cu_d3d11_get_device_fn d3d11_get_device;
    cu_ctx_create_fn ctx_create;
    cu_ctx_destroy_fn ctx_destroy;
    cu_mem_alloc_fn mem_alloc;
    cu_mem_free_fn mem_free;
    cu_memcpy_htod_fn memcpy_htod;
    cu_graphics_register_fn graphics_register;
    cu_graphics_map_fn graphics_map;
    cu_graphics_get_array_fn graphics_get_array;
    cu_memcpy_2d_fn memcpy_2d;
    cu_graphics_unmap_fn graphics_unmap;
    cu_graphics_unregister_fn graphics_unregister;
};

static int load_cuda(struct cuda_api *api) {
    HMODULE module = LoadLibraryA("nvcuda.dll");

    if (!module) {
        fprintf(stderr, "LoadLibrary(nvcuda.dll) failed: %lu\n", GetLastError());
        return 0;
    }
#define LOAD_FIELD(field, symbol) do { \
    FARPROC address = GetProcAddress(module, symbol); \
    memcpy(&api->field, &address, sizeof(api->field)); \
    if (!api->field) { \
        fprintf(stderr, "missing CUDA symbol: %s\n", symbol); \
        return 0; \
    } \
} while (0)
    LOAD_FIELD(init, "cuInit");
    LOAD_FIELD(device_get, "cuDeviceGet");
    LOAD_FIELD(d3d11_get_device, "cuD3D11GetDevice");
    LOAD_FIELD(ctx_create, "cuCtxCreate_v2");
    LOAD_FIELD(ctx_destroy, "cuCtxDestroy_v2");
    LOAD_FIELD(mem_alloc, "cuMemAlloc_v2");
    LOAD_FIELD(mem_free, "cuMemFree_v2");
    LOAD_FIELD(memcpy_htod, "cuMemcpyHtoD_v2");
    LOAD_FIELD(graphics_register, "cuGraphicsD3D11RegisterResource");
    LOAD_FIELD(graphics_map, "cuGraphicsMapResources");
    LOAD_FIELD(
        graphics_get_array, "cuGraphicsSubResourceGetMappedArray"
    );
    LOAD_FIELD(memcpy_2d, "cuMemcpy2D_v2");
    LOAD_FIELD(graphics_unmap, "cuGraphicsUnmapResources");
    LOAD_FIELD(graphics_unregister, "cuGraphicsUnregisterResource");
#undef LOAD_FIELD
    return 1;
}

static HRESULT create_device(
    ID3D11Device **device,
    ID3D11DeviceContext **context
) {
    D3D_FEATURE_LEVEL level;

    return D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        device,
        &level,
        context
    );
}

static int verify_texture(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    ID3D11Texture2D *shared_texture,
    const unsigned char *expected,
    UINT width,
    UINT rows
) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Texture2D *staging = NULL;
    HRESULT result;
    UINT row;
    int matches = 1;

    ID3D11Texture2D_GetDesc(shared_texture, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    result = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(result)) {
        fprintf(stderr, "CreateTexture2D(staging) failed: %#lx\n", result);
        return 0;
    }
    ID3D11DeviceContext_CopyResource(
        context, (ID3D11Resource *)staging, (ID3D11Resource *)shared_texture
    );
    result = ID3D11DeviceContext_Map(
        context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped
    );
    if (FAILED(result)) {
        fprintf(stderr, "Map(staging) failed: %#lx\n", result);
        ID3D11Texture2D_Release(staging);
        return 0;
    }
    for (row = 0; row < rows; ++row) {
        const unsigned char *actual =
            (const unsigned char *)mapped.pData + row * mapped.RowPitch;
        if (memcmp(actual, expected + row * width, width) != 0) {
            fprintf(
                stderr,
                "texture mismatch at row %u: got=%02x expected=%02x pitch=%u\n",
                row,
                actual[0],
                expected[row * width],
                mapped.RowPitch
            );
            matches = 0;
            break;
        }
    }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);
    return matches;
}


typedef CUresult (WINAPI *cu_stream_create_fn)(CUstream *, unsigned int);
typedef CUresult (WINAPI *cu_stream_fn)(CUstream);
typedef CUresult (WINAPI *cu_host_alloc_fn)(void **, size_t, unsigned int);
typedef CUresult (WINAPI *cu_host_free_fn)(void *);
typedef CUresult (WINAPI *cu_htod_async_fn)(CUdeviceptr, const void *, size_t, CUstream);
typedef CUresult (WINAPI *cu_copy_async_fn)(const CUDA_MEMCPY2D *, CUstream);

static int symbol(HMODULE module, const char *name, void *output, size_t size) {
    FARPROC p = GetProcAddress(module, name);
    if (!p || size != sizeof(p)) {
        fprintf(stderr, "missing CUDA symbol: %s\n", name);
        return 0;
    }
    memcpy(output, &p, size);
    return 1;
}

/* Submission on one device is not a completion fence for another device. */
static int wait_for_upload(ID3D11DeviceContext *context, ID3D11Query *event) {
    ULONGLONG deadline = GetTickCount64() + 5000;
    HRESULT hr;
    ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)event);
    ID3D11DeviceContext_Flush(context);
    while ((hr = ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)event,
                                            NULL, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
        if (GetTickCount64() >= deadline) { fprintf(stderr, "GPU fence timeout\n"); return 0; }
        Sleep(0);
    }
    return SUCCEEDED(hr);
}

int main(int argc, char **argv) {
    UINT width = 66, height = 64, frames = 120, warmup = 0, bytes = 1, rows;
    DXGI_FORMAT format = DXGI_FORMAT_NV12;
    const char *format_name = "NV12";
    int async = 0, partial = 0, check_bounds = 0, i, success = 0;
    struct cuda_api cuda = {0};
    cu_stream_create_fn stream_create = NULL;
    cu_stream_fn stream_sync = NULL, stream_destroy = NULL;
    cu_host_alloc_fn host_alloc = NULL;
    cu_host_free_fn host_free = NULL;
    cu_htod_async_fn htod_async = NULL;
    cu_copy_async_fn copy_async = NULL;
    HMODULE module;
    unsigned char *frame = NULL, *expected = NULL;
    size_t row_bytes, frame_size, index;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Device *producer_device = NULL, *consumer_device = NULL;
    ID3D11DeviceContext *producer_context = NULL, *consumer_context = NULL;
    ID3D11Texture2D *producer_texture = NULL, *consumer_texture = NULL;
    IDXGIResource *dxgi_resource = NULL;
    HANDLE shared_handle = NULL;
    ID3D11Query *completion = NULL;
    CUdevice cuda_device;
    CUcontext cuda_context = NULL;
    CUstream stream = NULL;
    CUdeviceptr cuda_frame = 0;
    CUgraphicsResource cuda_resource = NULL;
    CUarray cuda_array = NULL;
    CUDA_MEMCPY2D copy;
    HRESULT hr;
    CUresult result;
    LARGE_INTEGER frequency, start, end;
    double total_ms = 0, ready_ms = 0, max_ms = 0;
    UINT n;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--async")) async = 1;
        else if (!strcmp(argv[i], "--partial")) partial = 1;
        else if (!strcmp(argv[i], "--check-bounds")) check_bounds = 1;
        else if (i + 1 < argc && !strcmp(argv[i], "--width")) width = strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--height")) height = strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--frames")) frames = strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--warmup")) warmup = strtoul(argv[++i], NULL, 10);
        else if (i + 1 < argc && !strcmp(argv[i], "--format")) {
            format_name = argv[++i];
            if (!strcmp(format_name, "R8")) format = DXGI_FORMAT_R8_UNORM;
            else if (!strcmp(format_name, "RG8")) { format = DXGI_FORMAT_R8G8_UNORM; bytes = 2; }
            else if (!strcmp(format_name, "R16")) { format = DXGI_FORMAT_R16_UNORM; bytes = 2; }
            else if (!strcmp(format_name, "RG16")) { format = DXGI_FORMAT_R16G16_UNORM; bytes = 4; }
            else if (!strcmp(format_name, "NV12")) format = DXGI_FORMAT_NV12;
            else if (!strcmp(format_name, "P010")) { format = DXGI_FORMAT_P010; bytes = 2; }
            else { fprintf(stderr, "unsupported test format\n"); return 2; }
        } else { fprintf(stderr, "unknown/missing option: %s\n", argv[i]); return 2; }
    }
    if (width < 16 || width > 8192 || height < 16 || height > 8192 ||
        !frames || frames > 10000 || warmup > 1000 ||
        ((format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010) && ((width | height) & 1))) {
        fprintf(stderr, "invalid test dimensions/count\n"); return 2;
    }
    rows = height + ((format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010) ? height / 2 : 0);
    row_bytes = (size_t)width * bytes;
    frame_size = row_bytes * rows;
    expected = calloc(1, frame_size);
    if (!expected || !load_cuda(&cuda)) goto cleanup;
    module = GetModuleHandleA("nvcuda.dll");
#define LOAD_EXTRA(field, name) do { if (!symbol(module, name, &field, sizeof(field))) goto cleanup; } while (0)
    LOAD_EXTRA(stream_create, "cuStreamCreate");
    LOAD_EXTRA(stream_sync, "cuStreamSynchronize");
    LOAD_EXTRA(stream_destroy, "cuStreamDestroy_v2");
    LOAD_EXTRA(host_alloc, "cuMemHostAlloc");
    LOAD_EXTRA(host_free, "cuMemFreeHost");
    LOAD_EXTRA(htod_async, "cuMemcpyHtoDAsync_v2");
    LOAD_EXTRA(copy_async, "cuMemcpy2DAsync_v2");
#undef LOAD_EXTRA
    if ((result = cuda.init(0)) != CUDA_SUCCESS ||
        (result = cuda.device_get(&cuda_device, 0)) != CUDA_SUCCESS ||
        (result = cuda.ctx_create(&cuda_context, 0, cuda_device)) != CUDA_SUCCESS ||
        (result = host_alloc((void **)&frame, frame_size, 0)) != CUDA_SUCCESS ||
        (result = cuda.mem_alloc(&cuda_frame, frame_size)) != CUDA_SUCCESS ||
        (async && (result = stream_create(&stream, 1)) != CUDA_SUCCESS)) {
        fprintf(stderr, "CUDA initialization failed: %d\n", result); goto cleanup;
    }
    hr = create_device(&producer_device, &producer_context);
    if (FAILED(hr)) { fprintf(stderr, "producer device failed: %#lx\n", hr); goto cleanup; }
    hr = create_device(&consumer_device, &consumer_context);
    if (FAILED(hr)) { fprintf(stderr, "consumer device failed: %#lx\n", hr); goto cleanup; }
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = ID3D11Device_CreateTexture2D(producer_device, &desc, NULL, &producer_texture);
    if (FAILED(hr)) { fprintf(stderr, "shared texture failed: %#lx\n", hr); goto cleanup; }
    hr = ID3D11Texture2D_QueryInterface(producer_texture, &IID_IDXGIResource, (void **)&dxgi_resource);
    if (FAILED(hr) || FAILED(IDXGIResource_GetSharedHandle(dxgi_resource, &shared_handle))) goto cleanup;
    hr = ID3D11Device_OpenSharedResource(consumer_device, shared_handle, &IID_ID3D11Texture2D,
                                       (void **)&consumer_texture);
    if (FAILED(hr)) { fprintf(stderr, "consumer open failed: %#lx\n", hr); goto cleanup; }
    result = cuda.graphics_register(&cuda_resource, (ID3D11Resource *)producer_texture, 0);
    if (result != CUDA_SUCCESS) { fprintf(stderr, "register failed: %d\n", result); goto cleanup; }
    {
        D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
        hr = ID3D11Device_CreateQuery(producer_device, &query_desc, &completion);
        if (FAILED(hr)) { fprintf(stderr, "completion query failed\n"); goto cleanup; }
    }
    QueryPerformanceFrequency(&frequency);
    for (n = 0; n < frames + warmup; ++n) {
        double elapsed;
        UINT y;
        for (index = 0; index < frame_size; ++index) frame[index] = (unsigned char)(index * 13 + n * 29);
        result = async ? htod_async(cuda_frame, frame, frame_size, stream) :
                         cuda.memcpy_htod(cuda_frame, frame, frame_size);
        if (result != CUDA_SUCCESS) { fprintf(stderr, "input copy failed: %d\n", result); goto cleanup; }
        QueryPerformanceCounter(&start);
        if ((result = cuda.graphics_map(1, &cuda_resource, stream)) != CUDA_SUCCESS ||
            (result = cuda.graphics_get_array(&cuda_array, cuda_resource, 0, 0)) != CUDA_SUCCESS) goto upload_error;
        ZeroMemory(&copy, sizeof(copy));
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE; copy.srcDevice = cuda_frame; copy.srcPitch = row_bytes;
        copy.dstMemoryType = CU_MEMORYTYPE_ARRAY; copy.dstArray = cuda_array;
        copy.WidthInBytes = partial ? row_bytes / 2 : row_bytes;
        copy.Height = partial ? rows - 6 : rows;
        copy.dstXInBytes = partial ? bytes * 3 : 0; copy.dstY = partial ? 3 : 0;
        if (check_bounds && n == 0) {
            CUDA_MEMCPY2D invalid = copy;
            invalid.dstXInBytes = (size_t)-1;
            result = cuda.memcpy_2d(&invalid);
            if (result != 1) { fprintf(stderr, "overflow bounds check failed: %d\n", result); goto cleanup; }
            invalid = copy; invalid.dstY = rows; invalid.Height = 1;
            result = cuda.memcpy_2d(&invalid);
            if (result != 1) { fprintf(stderr, "row bounds check failed: %d\n", result); goto cleanup; }
        }
        result = async ? copy_async(&copy, stream) : cuda.memcpy_2d(&copy);
        if (result != CUDA_SUCCESS ||
            (result = cuda.graphics_unmap(1, &cuda_resource, stream)) != CUDA_SUCCESS) goto upload_error;
        QueryPerformanceCounter(&end);
        elapsed = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
        if (n >= warmup) { total_ms += elapsed; if (elapsed > max_ms) max_ms = elapsed; }
        if (!wait_for_upload(producer_context, completion)) goto cleanup;
        QueryPerformanceCounter(&end);
        if (n >= warmup) ready_ms += (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
        for (y = 0; y < copy.Height; ++y)
            memcpy(expected + (copy.dstY + y) * row_bytes + copy.dstXInBytes,
                   frame + y * row_bytes, copy.WidthInBytes);
        if (!verify_texture(consumer_device, consumer_context, consumer_texture, expected, row_bytes, rows)) {
            fprintf(stderr, "pixel verification failed on frame %u\n", n); goto cleanup;
        }
        continue;
    upload_error:
        fprintf(stderr, "upload failed on frame %u: %d\n", n, result); goto cleanup;
    }
    printf("UPLOAD PASS format=%s width=%u height=%u frames=%u warmup=%u async=%d partial=%d mean_bridge_ms=%.4f mean_ready_ms=%.4f max_bridge_ms=%.4f\n",
           format_name, width, height, frames, warmup, async, partial, total_ms / frames, ready_ms / frames, max_ms);
    success = 1;
cleanup:
    if (stream && stream_sync) stream_sync(stream);
    if (cuda_resource) cuda.graphics_unregister(cuda_resource);
    if (cuda_frame) cuda.mem_free(cuda_frame);
    if (frame && host_free) host_free(frame);
    if (stream && stream_destroy) stream_destroy(stream);
    if (cuda_context) cuda.ctx_destroy(cuda_context);
    if (completion) ID3D11Query_Release(completion);
    if (dxgi_resource) IDXGIResource_Release(dxgi_resource);
    if (consumer_texture) ID3D11Texture2D_Release(consumer_texture);
    if (producer_texture) ID3D11Texture2D_Release(producer_texture);
    if (consumer_context) ID3D11DeviceContext_Release(consumer_context);
    if (consumer_device) ID3D11Device_Release(consumer_device);
    if (producer_context) ID3D11DeviceContext_Release(producer_context);
    if (producer_device) ID3D11Device_Release(producer_device);
    free(expected);
    return success ? 0 : 1;
}
