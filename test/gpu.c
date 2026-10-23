/* End-to-end check on a real GPU: a DLSS-style upscale request through
 * nvngx.dll, which should be answered by FSR 4. Renders nothing; it
 * uploads a test image (render size), asks for an upscale to output size,
 * reads the result back and writes it to out.ppm.
 * Usage: gpu.exe [path\to\nvngx.dll] */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stdio.h>
#include <math.h>

#define RW 1280
#define RH 720
#define OW 2560
#define OH 1440

typedef unsigned (__cdecl *pfn_init)(unsigned long long, const wchar_t *, void *, int);
typedef unsigned (__cdecl *pfn_pp)(void **);
typedef unsigned (__cdecl *pfn_p)(void *);
typedef unsigned (__cdecl *pfn_create)(void *, int, void *, void **);
typedef unsigned (__cdecl *pfn_eval)(void *, void *, void *, void *);
typedef void (__cdecl *pfn_setui)(void *, const char *, unsigned);
typedef void (__cdecl *pfn_seti)(void *, const char *, int);
typedef void (__cdecl *pfn_setf)(void *, const char *, float);
typedef void (__cdecl *pfn_setr)(void *, const char *, void *);

static ID3D12Device *dev;
static ID3D12CommandQueue *queue;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static ID3D12Fence *fence;
static UINT64 fence_val;
static HANDLE fence_ev;

#define DIE(...) do { printf("FAIL " __VA_ARGS__); printf("\n"); exit(1); } while (0)
#define HR(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) DIE("%s: 0x%08lx", #x, (unsigned long)hr_); } while (0)

static void submit_wait(void)
{
    HR(ID3D12GraphicsCommandList_Close(list));
    ID3D12CommandList *l = (ID3D12CommandList *)list;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &l);
    HR(ID3D12CommandQueue_Signal(queue, fence, ++fence_val));
    HR(ID3D12Fence_SetEventOnCompletion(fence, fence_val, fence_ev));
    if (WaitForSingleObject(fence_ev, 10000) != WAIT_OBJECT_0)
        DIE("GPU did not finish within 10 s (device removed? reason 0x%08lx)", (unsigned long)ID3D12Device_GetDeviceRemovedReason(dev));
    HR(ID3D12CommandAllocator_Reset(alloc));
    HR(ID3D12GraphicsCommandList_Reset(list, alloc, NULL));
}

static ID3D12Resource *texture(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES st)
{
    D3D12_HEAP_PROPERTIES hp = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = w, .Height = h,
                             .DepthOrArraySize = 1, .MipLevels = 1, .Format = fmt, .SampleDesc = {1, 0}, .Flags = flags};
    ID3D12Resource *r = NULL;
    HR(ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &d, st, NULL, &IID_ID3D12Resource, (void **)&r));
    return r;
}

static ID3D12Resource *buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES st)
{
    D3D12_HEAP_PROPERTIES hp = {.Type = type};
    D3D12_RESOURCE_DESC d = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = size, .Height = 1,
                             .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {1, 0}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *r = NULL;
    HR(ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &d, st, NULL, &IID_ID3D12Resource, (void **)&r));
    return r;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                .Transition = {.pResource = r, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                               .StateBefore = from, .StateAfter = to}};
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
}

/* half-float encode, enough for [0, 65504] */
static unsigned short half(float f)
{
    union { float f; unsigned u; } v = {f};
    unsigned s = (v.u >> 16) & 0x8000, e = ((v.u >> 23) & 0xff), m = v.u & 0x7fffff;
    if (e < 113) return (unsigned short)s;
    if (e > 142) return (unsigned short)(s | 0x7bff);
    return (unsigned short)(s | ((e - 112) << 10) | (m >> 13));
}
static float unhalf(unsigned short h)
{
    unsigned s = (h & 0x8000) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    union { unsigned u; float f; } v;
    if (!e) return 0.0f;
    v.u = s | ((e + 112) << 23) | (m << 13);
    return v.f;
}

/* upload a w*h image of bpp bytes per pixel produced by fill() into tex */
static void upload(ID3D12Resource *tex, UINT w, UINT h, DXGI_FORMAT fmt, UINT bpp, void (*fill)(BYTE *, UINT, UINT, UINT))
{
    UINT pitch = (w * bpp + 255) & ~255u;
    ID3D12Resource *up = buffer((UINT64)pitch * h, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    BYTE *p;
    HR(ID3D12Resource_Map(up, 0, NULL, (void **)&p));
    fill(p, w, h, pitch);
    ID3D12Resource_Unmap(up, 0, NULL);
    D3D12_TEXTURE_COPY_LOCATION dst = {.pResource = tex, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION src = {.pResource = up, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                                       .PlacedFootprint = {0, {fmt, w, h, 1, pitch}}};
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
    submit_wait();
    ID3D12Resource_Release(up);
}

/* a test card: colour gradient, a checkerboard and a thin diagonal line */
static void fill_color(BYTE *p, UINT w, UINT h, UINT pitch)
{
    for (UINT y = 0; y < h; y++) {
        unsigned short *row = (unsigned short *)(p + (size_t)y * pitch);
        for (UINT x = 0; x < w; x++) {
            float r = (float)x / w, g = (float)y / h, b = ((x / 40 + y / 40) & 1) ? 0.9f : 0.1f;
            if (abs((int)x - (int)(y * w / h)) < 2) r = g = b = 1.0f;
            row[x * 4 + 0] = half(r); row[x * 4 + 1] = half(g);
            row[x * 4 + 2] = half(b); row[x * 4 + 3] = half(1.0f);
        }
    }
}
static void fill_depth(BYTE *p, UINT w, UINT h, UINT pitch)
{
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++)
            ((float *)(p + (size_t)y * pitch))[x] = 0.5f;
}
static void fill_zero(BYTE *p, UINT w, UINT h, UINT pitch) { memset(p, 0, (size_t)pitch * h); }

int main(int argc, char **argv)
{
    const char *dllpath = argc > 1 ? argv[1] : "nvngx.dll";
    HR(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev));
    D3D12_COMMAND_QUEUE_DESC qd = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    HR(ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue));
    HR(ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc));
    HR(ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list));
    HR(ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    fence_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    printf("ok   D3D12 device\n");

    ID3D12Resource *color = texture(RW, RH, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *depth = texture(RW, RH, DXGI_FORMAT_R32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *mv = texture(RW, RH, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *out = texture(OW, OH, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    upload(color, RW, RH, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, fill_color);
    upload(depth, RW, RH, DXGI_FORMAT_R32_FLOAT, 4, fill_depth);
    upload(mv, RW, RH, DXGI_FORMAT_R16G16_FLOAT, 4, fill_zero);
    barrier(color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(mv, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    submit_wait();
    printf("ok   test inputs uploaded (%dx%d)\n", RW, RH);

    HMODULE m = LoadLibraryA(dllpath);
    if (!m) DIE("load %s (%lu)", dllpath, GetLastError());
#define SYM(t, n, s) t n = (t)(void *)GetProcAddress(m, s); if (!n) DIE("export %s", s)
    SYM(pfn_init, Init, "NVSDK_NGX_D3D12_Init");
    SYM(pfn_pp, Alloc, "NVSDK_NGX_D3D12_AllocateParameters");
    SYM(pfn_p, Destroy, "NVSDK_NGX_D3D12_DestroyParameters");
    SYM(pfn_create, Create, "NVSDK_NGX_D3D12_CreateFeature");
    SYM(pfn_eval, Eval, "NVSDK_NGX_D3D12_EvaluateFeature");
    SYM(pfn_p, Release, "NVSDK_NGX_D3D12_ReleaseFeature");
    SYM(pfn_p, Shutdown1, "NVSDK_NGX_D3D12_Shutdown1");
    SYM(pfn_setui, SetUI, "NVSDK_NGX_Parameter_SetUI");
    SYM(pfn_seti, SetI, "NVSDK_NGX_Parameter_SetI");
    SYM(pfn_setf, SetF, "NVSDK_NGX_Parameter_SetF");
    SYM(pfn_setr, SetR, "NVSDK_NGX_Parameter_SetD3d12Resource");

    if (Init(1, L"C:\\temp", dev, 0x15) != 1) DIE("Init");
    void *p = NULL, *h = NULL;
    Alloc(&p);
    SetUI(p, "Width", RW); SetUI(p, "Height", RH);
    SetUI(p, "OutWidth", OW); SetUI(p, "OutHeight", OH);
    SetI(p, "PerfQualityValue", 0);
    SetI(p, "DLSS.Feature.Create.Flags", 2 /* MVLowRes */);
    unsigned rc = Create(list, 1, p, &h);
    if (rc != 1 || !h) DIE("CreateFeature rc=0x%x (see ngx-fsr4.log)", rc);
    submit_wait();
    printf("ok   CreateFeature -> FSR context\n");

    static const float jit[8][2] = {{0, -0.1667f}, {-0.25f, 0.1667f}, {0.25f, -0.3889f}, {-0.375f, -0.0556f},
                                    {0.125f, 0.2778f}, {-0.125f, -0.2778f}, {0.375f, 0.0556f}, {-0.4375f, 0.3889f}};
    for (int n = 0; n < 16; n++) {
        SetR(p, "Color", color); SetR(p, "Depth", depth); SetR(p, "MotionVectors", mv); SetR(p, "Output", out);
        SetF(p, "Jitter.Offset.X", jit[n % 8][0]); SetF(p, "Jitter.Offset.Y", jit[n % 8][1]);
        SetF(p, "MV.Scale.X", 1); SetF(p, "MV.Scale.Y", 1);
        SetI(p, "Reset", n == 0);
        SetF(p, "FrameTimeDeltaInMsec", 16.7f);
        rc = Eval(list, h, p, NULL);
        if (rc != 1) DIE("EvaluateFeature #%d rc=0x%x", n + 1, rc);
        submit_wait();
    }
    printf("ok   16 frames evaluated on the GPU\n");

    /* read back */
    UINT pitch = (OW * 8 + 255) & ~255u;
    ID3D12Resource *rb = buffer((UINT64)pitch * OH, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {.pResource = out, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst = {.pResource = rb, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                                       .PlacedFootprint = {0, {DXGI_FORMAT_R16G16B16A16_FLOAT, OW, OH, 1, pitch}}};
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
    submit_wait();
    BYTE *px;
    HR(ID3D12Resource_Map(rb, 0, NULL, (void **)&px));
    FILE *ppm = fopen("out.ppm", "wb");
    fprintf(ppm, "P6\n%d %d\n255\n", OW, OH);
    double err = 0, sum = 0;
    int samples = 0;
    for (UINT y = 0; y < OH; y++) {
        unsigned short *row = (unsigned short *)(px + (size_t)y * pitch);
        for (UINT x = 0; x < OW; x++) {
            float r = unhalf(row[x * 4]), g = unhalf(row[x * 4 + 1]), b = unhalf(row[x * 4 + 2]);
            BYTE c[3] = {(BYTE)(fminf(fmaxf(r, 0), 1) * 255), (BYTE)(fminf(fmaxf(g, 0), 1) * 255), (BYTE)(fminf(fmaxf(b, 0), 1) * 255)};
            fwrite(c, 1, 3, ppm);
            sum += r + g + b;
            /* compare the smooth red/green gradient away from edges */
            if (x % 64 == 32 && y % 64 == 32) {
                float er = (float)x / OW, eg = (float)y / OH;
                err += fabs(r - er) + fabs(g - eg);
                samples++;
            }
        }
    }
    fclose(ppm);
    ID3D12Resource_Unmap(rb, 0, NULL);
    double mean_err = err / (2.0 * samples);
    printf("%s output non-black (mean channel %.3f)\n", sum > 1000 ? "ok  " : "FAIL", sum / (3.0 * OW * OH));
    printf("%s matches the test card: mean gradient error %.4f\n", mean_err < 0.05 ? "ok  " : "FAIL", mean_err);

    Release(h);
    Destroy(p);
    Shutdown1(dev);
    printf("%s\n", (sum > 1000 && mean_err < 0.05) ? "PASSED" : "FAILED");
    return !(sum > 1000 && mean_err < 0.05);
}
