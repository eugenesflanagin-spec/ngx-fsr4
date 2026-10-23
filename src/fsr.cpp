// FSR backend: answers a DLSS Super Resolution evaluate with an FSR 4
// upscale, through AMD's public FidelityFX API (ffxCreateContext /
// ffxDispatch), loaded at run time from amd_fidelityfx_upscaler_dx12.dll.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/ffx/upscalers/include/ffx_upscale.h"
#include "../third_party/ffx/api/include/dx12/ffx_api_dx12.h"

#include "ngx_fsr4.h"

namespace {

PfnFfxCreateContext create_ctx;
PfnFfxDestroyContext destroy_ctx;
PfnFfxDispatch dispatch;
PfnFfxQuery query;
HMODULE ffx_dll;
bool load_tried;

// the directory our own DLL lives in, with a trailing backslash
void self_dir(wchar_t *out, size_t n)
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_dir), &self);
    DWORD len = GetModuleFileNameW(self, out, (DWORD)n);
    while (len && out[len - 1] != L'\\' && out[len - 1] != L'/')
        len--;
    out[len] = 0;
}

bool load_ffx()
{
    if (load_tried)
        return ffx_dll != nullptr;
    load_tried = true;
    wchar_t path[MAX_PATH * 2];
    if (GetEnvironmentVariableW(L"NGX_FSR4_FFX_DLL", path, MAX_PATH)) {
        ffx_dll = LoadLibraryW(path);
    } else {
        // a unique name first: a same-named DLL already loaded (e.g. by OptiScaler)
        // would be returned instead of ours
        static const wchar_t *const names[] = {L"ngx-fsr4-upscaler.dll", L"amd_fidelityfx_upscaler_dx12.dll", L"amd_fidelityfx_dx12.dll"};
        for (const wchar_t *name : names) {
            self_dir(path, MAX_PATH);
            wcsncat(path, name, MAX_PATH);
            if ((ffx_dll = LoadLibraryW(path)))
                break;
        }
    }
    if (!ffx_dll) {
        ngx_log("FSR: could not load the FidelityFX upscaler DLL (error %lu)", GetLastError());
        return false;
    }
    GetModuleFileNameW(ffx_dll, path, MAX_PATH);
    ngx_log("FSR: loaded %ls", path);
    create_ctx = (PfnFfxCreateContext)(void *)GetProcAddress(ffx_dll, "ffxCreateContext");
    destroy_ctx = (PfnFfxDestroyContext)(void *)GetProcAddress(ffx_dll, "ffxDestroyContext");
    dispatch = (PfnFfxDispatch)(void *)GetProcAddress(ffx_dll, "ffxDispatch");
    query = (PfnFfxQuery)(void *)GetProcAddress(ffx_dll, "ffxQuery");
    if (!create_ctx || !destroy_ctx || !dispatch || !query) {
        ngx_log("FSR: DLL lacks the ffx* exports");
        FreeLibrary(ffx_dll);
        ffx_dll = nullptr;
        return false;
    }
    return true;
}

// Pick the newest FSR 4.x the DLL offers, or the one NGX_FSR4_VERSION names.
uint64_t pick_version(ID3D12Device *dev)
{
    uint64_t count = 0;
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    q.device = dev;
    q.outputCount = &count;
    if (query(nullptr, &q.header) != FFX_API_RETURN_OK || !count) {
        ngx_log("FSR: version query failed; using the DLL's default");
        return 0;
    }
    uint64_t ids[32] = {};
    const char *names[32] = {};
    if (count > 32)
        count = 32;
    q.versionIds = ids;
    q.versionNames = names;
    query(nullptr, &q.header);

    char want[32] = {};
    GetEnvironmentVariableA("NGX_FSR4_VERSION", want, sizeof want);
    uint64_t chosen = 0;
    const char *chosen_name = "(default)";
    for (uint64_t i = 0; i < count; i++) {
        const char *n = names[i] ? names[i] : "?";
        ngx_log("FSR: version offered: %s (id 0x%llx)", n, (unsigned long long)ids[i]);
        bool match = want[0] ? strstr(n, want) != nullptr : strncmp(n, "4.", 2) == 0 || strstr(n, " 4.") != nullptr;
        // the list is newest first; keep the first match
        if (match && !chosen) {
            chosen = ids[i];
            chosen_name = n;
        }
    }
    ngx_log("FSR: using %s", chosen_name);
    return chosen;
}

float env_float(const char *name, float fallback)
{
    char buf[32];
    if (GetEnvironmentVariableA(name, buf, sizeof buf))
        return (float)atof(buf);
    return fallback;
}

struct Fsr {
    ffxContext ctx = nullptr;
    bool depth_inverted = false;
    LARGE_INTEGER last{};
};

} // namespace

extern "C" void *fsr_create(ID3D12Device *dev, const struct fsr_create_info *ci)
{
    if (!dev) {
        ngx_log("FSR: no D3D12 device from Init; FSR disabled (logging only)");
        return nullptr;
    }
    if (!load_ffx())
        return nullptr;

    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = dev;

    ffxOverrideVersion ver{};
    ver.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    ver.versionId = pick_version(dev);

    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext = &backend.header;
    if (ver.versionId)
        backend.header.pNext = &ver.header;

    // DLSS create flags -> FSR enable flags
    int f = ci->ngx_flags;
    uint32_t flags = 0;
    if (f & (1 << 0)) flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
    if (!(f & (1 << 1))) flags |= FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
    if (f & (1 << 2)) flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (f & (1 << 3)) flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    if (f & (1 << 6)) flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    // dynamic resolution is allowed whenever max render < output
    flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
    desc.flags = flags;
    desc.maxRenderSize = {ci->max_render_w, ci->max_render_h};
    desc.maxUpscaleSize = {ci->out_w, ci->out_h};

    auto *s = new Fsr;
    s->depth_inverted = f & (1 << 3);
    ffxReturnCode_t rc = create_ctx(&s->ctx, &desc.header, nullptr);
    ngx_log("FSR: ffxCreateContext max render %ux%u -> %ux%u flags 0x%x: rc=%u ctx=%p",
            ci->max_render_w, ci->max_render_h, ci->out_w, ci->out_h, flags, rc, s->ctx);
    if (rc != FFX_API_RETURN_OK) {
        delete s;
        return nullptr;
    }
    return s;
}

extern "C" int fsr_dispatch(void *handle, ID3D12GraphicsCommandList *cl, const struct fsr_eval_info *e)
{
    auto *s = static_cast<Fsr *>(handle);
    if (!s || !cl || !e->color || !e->output)
        return 0;

    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = cl;
    d.color = ffxApiGetResourceDX12(e->color);
    d.depth = ffxApiGetResourceDX12(e->depth);
    d.motionVectors = ffxApiGetResourceDX12(e->motion);
    d.exposure = ffxApiGetResourceDX12(e->exposure);
    d.reactive = ffxApiGetResourceDX12(e->reactive);
    d.transparencyAndComposition = ffxApiGetResourceDX12(e->transparency);
    d.output = ffxApiGetResourceDX12(e->output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    float jsign = env_float("NGX_FSR4_JITTER_SIGN", 1.0f);
    d.jitterOffset = {e->jitter_x * jsign, e->jitter_y * jsign};
    d.motionVectorScale = {e->mv_scale_x, e->mv_scale_y};
    d.renderSize = {e->render_w, e->render_h};
    d.upscaleSize = {e->out_w, e->out_h};
    d.enableSharpening = e->sharpness > 0.0f;
    d.sharpness = e->sharpness;

    float dt = e->frame_ms;
    // DLSS wants milliseconds, but some games (Boundary) pass seconds;
    // nothing renders a frame in under 0.1 ms
    if (dt > 0.0f && dt < 0.1f)
        dt *= 1000.0f;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (dt <= 0.0f)
        dt = s->last.QuadPart ? (float)((now.QuadPart - s->last.QuadPart) * 1000.0 / freq.QuadPart) : 16.7f;
    s->last = now;
    d.frameTimeDelta = dt;
    d.preExposure = e->pre_exposure > 0.0f ? e->pre_exposure : 1.0f;
    d.reset = e->reset != 0;

    // DLSS has no camera info; FSR wants it. Defaults like OptiScaler's,
    // overridable per game.
    float cam_near = env_float("NGX_FSR4_NEAR", 0.1f), cam_far = env_float("NGX_FSR4_FAR", 100000.0f);
    d.cameraNear = s->depth_inverted ? cam_far : cam_near;
    d.cameraFar = s->depth_inverted ? cam_near : cam_far;
    d.cameraFovAngleVertical = env_float("NGX_FSR4_FOV", 1.0471976f);
    d.viewSpaceToMetersFactor = 1.0f;
    d.flags = 0;

    ffxReturnCode_t rc = dispatch(&s->ctx, &d.header);
    if (rc != FFX_API_RETURN_OK) {
        ngx_log("FSR: ffxDispatch rc=%u", rc);
        return 0;
    }
    return 1;
}

extern "C" void fsr_destroy(void *handle)
{
    auto *s = static_cast<Fsr *>(handle);
    if (!s)
        return;
    if (s->ctx)
        destroy_ctx(&s->ctx, nullptr);
    delete s;
}
