/*
 * ngx-fsr4: a stand-in for NVIDIA's _nvngx.dll that answers DLSS Super
 * Resolution (NGX) calls with an FSR 4 upscale (fsr.cpp), logging every
 * call and parameter to ngx-fsr4.log beside the DLL.
 *
 * Written from NVIDIA's public NGX SDK headers only. The parameter object
 * is a hand-built vtable in the MSVC order, as dumped by clang for
 * x86_64-pc-windows-msvc (tools/dump-vtable.sh):
 *   0..7  Set(void*, D3D12*, D3D11*, int, uint, double, float, ull)
 *   8..15 Get(same order)
 *   16    Reset
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ngx_fsr4.h"

#ifndef NGX_FSR4_VERSION
#define NGX_FSR4_VERSION "dev"
#endif

static volatile LONG g_total_evals; /* Set/Get lines stop after a few frames */

/* ---- result codes / constants (nvsdk_ngx_defs.h) ---- */
#define NGX_OK 0x1u
#define NGX_FAIL 0xBAD00000u
#define NGX_FAIL_FEATURE_NOT_SUPPORTED (NGX_FAIL | 1)
#define NGX_FAIL_FEATURE_NOT_FOUND (NGX_FAIL | 4)
#define NGX_FAIL_INVALID_PARAMETER (NGX_FAIL | 5)
#define NGX_FAIL_NOT_INITIALIZED (NGX_FAIL | 7)
#define NGX_FAIL_PLATFORM_ERROR (NGX_FAIL | 2)

#define FEATURE_SUPERSAMPLING 1
#define FEATURE_FRAMEGEN 11
#define FEATURE_RAYRECON 13

/* ---- logging ---- */
static CRITICAL_SECTION g_log_cs;
static FILE *g_log;
static LARGE_INTEGER g_qpf, g_qp0;

static void log_open(HMODULE self)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t *slash = NULL;
    for (DWORD i = 0; i < n; i++)
        if (path[i] == L'\\' || path[i] == L'/')
            slash = &path[i];
    if (slash && (size_t)(slash - path) + 16 < MAX_PATH)
        wcscpy(slash + 1, L"ngx-fsr4.log");
    else
        wcscpy(path, L"ngx-fsr4.log");
    g_log = _wfopen(path, L"a");
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qp0);
}

void ngx_log(const char *fmt, ...)
{
    if (!g_log)
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = (double)(now.QuadPart - g_qp0.QuadPart) / (double)g_qpf.QuadPart;
    EnterCriticalSection(&g_log_cs);
    fprintf(g_log, "%10.4f [%5lu] ", t, GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_cs);
}

/* NGX "E-parameters" are "#" + one byte; print them readably. */
static const char *key_str(const char *k, char *buf, size_t n)
{
    if (!k)
        return "(null)";
    if (k[0] == '#' && k[1] && !k[2]) {
        snprintf(buf, n, "#x%02x", (unsigned char)k[1]);
        return buf;
    }
    return k;
}

/* ---- parameter store ---- */
enum vt { V_VOID, V_D12, V_D11, V_I, V_UI, V_D, V_F, V_ULL };
static const char *const vt_name[] = {"void*", "d3d12", "d3d11", "int", "uint", "double", "float", "ull"};

struct entry {
    char key[96];
    enum vt type;
    union {
        void *p;
        int i;
        unsigned u;
        double d;
        float f;
        unsigned long long ull;
    } v;
};

struct params {
    const void *const *vtbl; /* must be first: the game calls through it */
    CRITICAL_SECTION cs;
    struct entry *e;
    int n, cap;
    int persistent; /* capability object: DestroyParameters keeps it */
    int id;
};

/* E-parameter aliases → canonical names, so Get works with either form. */
static const struct { unsigned char code; const char *name; } aliases[] = {
    {0x01, "SuperSampling.Available"}, {0x10, "Width"}, {0x11, "Height"},
    {0x12, "OutWidth"}, {0x13, "OutHeight"}, {0x14, "Sharpness"},
    {0x15, "Scratch"}, {0x16, "Scratch.SizeInBytes"}, {0x1e, "Color"},
    {0x22, "Output"}, {0x25, "Reset"}, {0x27, "MotionVectors"},
    {0x2c, "MV.Scale.X"}, {0x2d, "MV.Scale.Y"}, {0x35, "CreationNodeMask"},
    {0x36, "VisibilityNodeMask"}, {0x3d, "Depth"},
    {0x3e, "DLSSOptimalSettingsCallback"}, {0x3f, "PerfQualityValue"},
    {0x40, "RTXValue"},
};

static const char *canon(const char *k)
{
    if (k && k[0] == '#' && k[1] && !k[2])
        for (size_t i = 0; i < sizeof aliases / sizeof *aliases; i++)
            if ((unsigned char)k[1] == aliases[i].code)
                return aliases[i].name;
    return k;
}

static struct entry *find(struct params *p, const char *k)
{
    for (int i = 0; i < p->n; i++)
        if (!strcmp(p->e[i].key, k))
            return &p->e[i];
    return NULL;
}

static struct entry *slot(struct params *p, const char *k)
{
    struct entry *e = find(p, k);
    if (e)
        return e;
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        struct entry *ne = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof *ne);
        if (!ne)
            return NULL;
        if (p->e) {
            memcpy(ne, p->e, p->n * sizeof *ne);
            HeapFree(GetProcessHeap(), 0, p->e);
        }
        p->e = ne;
        p->cap = cap;
    }
    e = &p->e[p->n++];
    lstrcpynA(e->key, k, sizeof e->key);
    return e;
}

static int g_verbose_sets = 1; /* NGX_FSR4_QUIET=1 silences per-Set lines */

static int chatty(void) { return g_verbose_sets && g_total_evals < 3; }

static void put(struct params *p, const char *key, enum vt t, const void *val, int log)
{
    const char *k = canon(key);
    if (!k)
        return;
    EnterCriticalSection(&p->cs);
    struct entry *e = slot(p, k);
    if (e) {
        e->type = t;
        switch (t) {
        case V_VOID: case V_D12: case V_D11: e->v.p = *(void *const *)val; break;
        case V_I: e->v.i = *(const int *)val; break;
        case V_UI: e->v.u = *(const unsigned *)val; break;
        case V_D: e->v.d = *(const double *)val; break;
        case V_F: e->v.f = *(const float *)val; break;
        case V_ULL: e->v.ull = *(const unsigned long long *)val; break;
        }
    }
    LeaveCriticalSection(&p->cs);
    if (log) {
        char kb[16];
        switch (t) {
        case V_VOID: case V_D12: case V_D11:
            ngx_log("  P%d Set %-40s %-6s %p", p->id, key_str(key, kb, sizeof kb), vt_name[t], *(void *const *)val);
            break;
        case V_I: ngx_log("  P%d Set %-40s int    %d", p->id, key_str(key, kb, sizeof kb), *(const int *)val); break;
        case V_UI: ngx_log("  P%d Set %-40s uint   %u", p->id, key_str(key, kb, sizeof kb), *(const unsigned *)val); break;
        case V_D: ngx_log("  P%d Set %-40s double %g", p->id, key_str(key, kb, sizeof kb), *(const double *)val); break;
        case V_F: ngx_log("  P%d Set %-40s float  %g", p->id, key_str(key, kb, sizeof kb), (double)*(const float *)val); break;
        case V_ULL: ngx_log("  P%d Set %-40s ull    %llu", p->id, key_str(key, kb, sizeof kb), *(const unsigned long long *)val); break;
        }
    }
}

/* Get with conversion: games often GetF a value stored as int and so on. */
static unsigned get(struct params *p, const char *key, enum vt want, void *out, int log)
{
    const char *k = canon(key);
    char kb[16];
    if (!k || !out)
        return NGX_FAIL_INVALID_PARAMETER;
    EnterCriticalSection(&p->cs);
    struct entry *e = find(p, k);
    struct entry c;
    if (e)
        c = *e;
    LeaveCriticalSection(&p->cs);
    if (!e) {
        if (log)
            ngx_log("  P%d Get %-40s %-6s -> missing", p->id, key_str(key, kb, sizeof kb), vt_name[want]);
        return NGX_FAIL;
    }
    int ptr_have = c.type <= V_D11, ptr_want = want <= V_D11;
    if (ptr_have != ptr_want) {
        ngx_log("  P%d Get %-40s %-6s -> type mismatch (stored %s)", p->id, key_str(key, kb, sizeof kb), vt_name[want], vt_name[c.type]);
        return NGX_FAIL_INVALID_PARAMETER;
    }
    if (ptr_want) {
        *(void **)out = c.v.p;
    } else {
        double d;
        unsigned long long u;
        switch (c.type) {
        case V_I: d = c.v.i; u = (unsigned long long)(long long)c.v.i; break;
        case V_UI: d = c.v.u; u = c.v.u; break;
        case V_D: d = c.v.d; u = (unsigned long long)c.v.d; break;
        case V_F: d = c.v.f; u = (unsigned long long)c.v.f; break;
        default: d = (double)c.v.ull; u = c.v.ull; break;
        }
        switch (want) {
        case V_I: *(int *)out = (c.type == V_D || c.type == V_F) ? (int)d : (int)u; break;
        case V_UI: *(unsigned *)out = (unsigned)u; break;
        case V_D: *(double *)out = d; break;
        case V_F: *(float *)out = (float)d; break;
        default: *(unsigned long long *)out = u; break;
        }
    }
    if (log)
        ngx_log("  P%d Get %-40s %-6s -> ok", p->id, key_str(key, kb, sizeof kb), vt_name[want]);
    return NGX_OK;
}

/* vtable slots: `this` is the params object (RCX), name in RDX, value in
 * R8 (integers/pointers) or XMM2 (float/double) — the MS x64 ABI that
 * mingw-clang also uses for plain C. */
static void __cdecl s_void(struct params *p, const char *k, void *v) { put(p, k, V_VOID, &v, chatty()); }
static void __cdecl s_d12(struct params *p, const char *k, void *v) { put(p, k, V_D12, &v, chatty()); }
static void __cdecl s_d11(struct params *p, const char *k, void *v) { put(p, k, V_D11, &v, chatty()); }
static void __cdecl s_i(struct params *p, const char *k, int v) { put(p, k, V_I, &v, chatty()); }
static void __cdecl s_ui(struct params *p, const char *k, unsigned v) { put(p, k, V_UI, &v, chatty()); }
static void __cdecl s_d(struct params *p, const char *k, double v) { put(p, k, V_D, &v, chatty()); }
static void __cdecl s_f(struct params *p, const char *k, float v) { put(p, k, V_F, &v, chatty()); }
static void __cdecl s_ull(struct params *p, const char *k, unsigned long long v) { put(p, k, V_ULL, &v, chatty()); }
static unsigned __cdecl g_void(struct params *p, const char *k, void **o) { return get(p, k, V_VOID, o, chatty()); }
static unsigned __cdecl g_d12(struct params *p, const char *k, void **o) { return get(p, k, V_D12, o, chatty()); }
static unsigned __cdecl g_d11(struct params *p, const char *k, void **o) { return get(p, k, V_D11, o, chatty()); }
static unsigned __cdecl g_i(struct params *p, const char *k, int *o) { return get(p, k, V_I, o, chatty()); }
static unsigned __cdecl g_ui(struct params *p, const char *k, unsigned *o) { return get(p, k, V_UI, o, chatty()); }
static unsigned __cdecl g_d(struct params *p, const char *k, double *o) { return get(p, k, V_D, o, chatty()); }
static unsigned __cdecl g_f(struct params *p, const char *k, float *o) { return get(p, k, V_F, o, chatty()); }
static unsigned __cdecl g_ull(struct params *p, const char *k, unsigned long long *o) { return get(p, k, V_ULL, o, chatty()); }
static void __cdecl p_reset(struct params *p)
{
    ngx_log("  P%d Reset", p->id);
    EnterCriticalSection(&p->cs);
    p->n = 0;
    LeaveCriticalSection(&p->cs);
}

static const void *const params_vtbl[17] = {
    (void *)s_void, (void *)s_d12, (void *)s_d11, (void *)s_i,
    (void *)s_ui, (void *)s_d, (void *)s_f, (void *)s_ull,
    (void *)g_void, (void *)g_d12, (void *)g_d11, (void *)g_i,
    (void *)g_ui, (void *)g_d, (void *)g_f, (void *)g_ull,
    (void *)p_reset,
};

static volatile LONG g_param_ids;

static struct params *params_new(int persistent)
{
    struct params *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p);
    if (!p)
        return NULL;
    p->vtbl = params_vtbl;
    InitializeCriticalSection(&p->cs);
    p->persistent = persistent;
    p->id = (int)InterlockedIncrement(&g_param_ids);
    return p;
}

static void params_free(struct params *p)
{
    if (!p || p->persistent)
        return;
    DeleteCriticalSection(&p->cs);
    if (p->e)
        HeapFree(GetProcessHeap(), 0, p->e);
    HeapFree(GetProcessHeap(), 0, p);
}

/* Typed helpers for our own code (not logged as game Sets). */
static void set_i(struct params *p, const char *k, int v) { put(p, k, V_I, &v, 0); }
static void set_ui(struct params *p, const char *k, unsigned v) { put(p, k, V_UI, &v, 0); }
static void set_f(struct params *p, const char *k, float v) { put(p, k, V_F, &v, 0); }
static void set_ull(struct params *p, const char *k, unsigned long long v) { put(p, k, V_ULL, &v, 0); }
static void set_p(struct params *p, const char *k, void *v) { put(p, k, V_VOID, &v, 0); }

static int peek_ui(struct params *p, const char *k, unsigned *o) { return get(p, k, V_UI, o, 0) == NGX_OK; }
static int peek_i(struct params *p, const char *k, int *o) { return get(p, k, V_I, o, 0) == NGX_OK; }
static int peek_f(struct params *p, const char *k, float *o) { return get(p, k, V_F, o, 0) == NGX_OK; }
static void *peek_p(struct params *p, const char *k) { void *o = NULL; get(p, k, V_VOID, &o, 0); return o; }

/* ---- DLSS helper callbacks the game finds in the capability object ---- */
static float quality_ratio(int q)
{
    switch (q) {
    case 0: return 0.5f;      /* MaxPerf (Performance) */
    case 1: return 0.58f;     /* Balanced */
    case 2: return 2.0f / 3;  /* MaxQuality (Quality) */
    case 3: return 1.0f / 3;  /* UltraPerformance */
    case 4: return 0.77f;     /* UltraQuality */
    case 5: return 1.0f;      /* DLAA */
    default: return 2.0f / 3;
    }
}

static unsigned __cdecl optimal_settings(struct params *p)
{
    unsigned w = 0, h = 0;
    int q = 2;
    peek_ui(p, "Width", &w);
    peek_ui(p, "Height", &h);
    peek_i(p, "PerfQualityValue", &q);
    float r = quality_ratio(q);
    unsigned ow = (unsigned)(w * r + 0.5f), oh = (unsigned)(h * r + 0.5f);
    set_ui(p, "OutWidth", ow);
    set_ui(p, "OutHeight", oh);
    set_ui(p, "DLSS.Get.Dynamic.Max.Render.Width", w);
    set_ui(p, "DLSS.Get.Dynamic.Max.Render.Height", h);
    set_ui(p, "DLSS.Get.Dynamic.Min.Render.Width", (unsigned)(w / 3.0f + 0.5f));
    set_ui(p, "DLSS.Get.Dynamic.Min.Render.Height", (unsigned)(h / 3.0f + 0.5f));
    set_f(p, "Sharpness", 0.0f);
    ngx_log("OptimalSettings %ux%u quality=%d -> render %ux%u", w, h, q, ow, oh);
    return NGX_OK;
}

static unsigned __cdecl get_stats(struct params *p)
{
    set_ull(p, "SizeInBytes", 0);
    return NGX_OK;
}

static void fill_caps(struct params *p)
{
    static const char *const avail[] = {"SuperSampling.Available"};
    for (size_t i = 0; i < sizeof avail / sizeof *avail; i++)
        set_i(p, avail[i], 1);
    set_i(p, "SuperSampling.NeedsUpdatedDriver", 0);
    set_ui(p, "SuperSampling.MinDriverVersionMajor", 0);
    set_ui(p, "SuperSampling.MinDriverVersionMinor", 0);
    set_i(p, "SuperSampling.FeatureInitResult", (int)NGX_OK);
    /* frame generation and ray reconstruction: not offered (yet) */
    set_i(p, "FrameGeneration.Available", 0);
    set_i(p, "SuperSamplingDenoising.Available", 0);
    set_i(p, "RayReconstruction.Available", 0);
    set_p(p, "DLSSOptimalSettingsCallback", (void *)optimal_settings);
    set_p(p, "DLSSGetStatsCallback", (void *)get_stats);
}

/* ---- global state ---- */
static struct params *g_caps;
static ID3D12Device *g_device;

struct feature {
    unsigned Id; /* NVSDK_NGX_Handle's only public field */
    int kind;
    unsigned render_w, render_h, out_w, out_h;
    int flags, quality;
    unsigned evals;
    void *fsr; /* stage 3: FSR context */
};
static volatile LONG g_feature_ids;

static struct params *caps(void)
{
    if (!g_caps) {
        g_caps = params_new(1);
        if (g_caps)
            fill_caps(g_caps);
    }
    return g_caps;
}

static void log_flags(int f)
{
    ngx_log("  create flags 0x%x:%s%s%s%s%s%s", f,
            f & 1 ? " HDR" : "", f & 2 ? " MVLowRes" : " MVDisplayRes",
            f & 4 ? " MVJittered" : "", f & 8 ? " DepthInverted" : "",
            f & 32 ? " Sharpen" : "", f & 64 ? " AutoExposure" : "");
}

static void log_resource(const char *name, void *r)
{
    if (!r) {
        ngx_log("    %-22s (none)", name);
        return;
    }
    D3D12_RESOURCE_DESC d = ID3D12Resource_GetDesc((ID3D12Resource *)r);
    ngx_log("    %-22s %p %llux%u fmt=%d", name, r, (unsigned long long)d.Width, d.Height, (int)d.Format);
}

/* ---- exports: D3D12 ---- */
#define API __declspec(dllexport) unsigned __cdecl

static unsigned do_init(const char *fn, unsigned long long app_id, const wchar_t *path, ID3D12Device *dev, int ver)
{
    ngx_log("%s app=%llu sdk=0x%x device=%p data=%ls", fn, app_id, ver, (void *)dev, path ? path : L"(null)");
    g_device = dev;
    caps();
    return NGX_OK;
}

API NVSDK_NGX_D3D12_Init(unsigned long long app, const wchar_t *path, ID3D12Device *dev, int ver)
{
    return do_init(__func__, app, path, dev, ver);
}

API NVSDK_NGX_D3D12_Init_Ext(unsigned long long app, const wchar_t *path, ID3D12Device *dev, int ver, const void *params)
{
    (void)params;
    return do_init(__func__, app, path, dev, ver);
}

API NVSDK_NGX_D3D12_Init_with_ProjectID(const char *project, int engine, const char *engine_ver, const wchar_t *path, ID3D12Device *dev, int ver, const void *info)
{
    (void)info;
    ngx_log("%s project=%s engine=%d/%s", __func__, project ? project : "(null)", engine, engine_ver ? engine_ver : "(null)");
    return do_init(__func__, 0, path, dev, ver);
}

API NVSDK_NGX_D3D12_Shutdown(void)
{
    ngx_log("%s", __func__);
    return NGX_OK;
}

API NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *dev)
{
    ngx_log("%s device=%p", __func__, (void *)dev);
    return NGX_OK;
}

API NVSDK_NGX_D3D12_GetParameters(struct params **out)
{
    ngx_log("%s", __func__);
    if (!out)
        return NGX_FAIL_INVALID_PARAMETER;
    *out = caps();
    return *out ? NGX_OK : NGX_FAIL;
}

API NVSDK_NGX_D3D12_GetCapabilityParameters(struct params **out)
{
    ngx_log("%s", __func__);
    if (!out)
        return NGX_FAIL_INVALID_PARAMETER;
    /* a fresh object the caller destroys, already holding the answers */
    struct params *p = params_new(0);
    if (!p)
        return NGX_FAIL;
    fill_caps(p);
    *out = p;
    ngx_log("  -> P%d", p->id);
    return NGX_OK;
}

API NVSDK_NGX_D3D12_AllocateParameters(struct params **out)
{
    if (!out)
        return NGX_FAIL_INVALID_PARAMETER;
    *out = params_new(0);
    ngx_log("%s -> P%d", __func__, *out ? (*out)->id : -1);
    return *out ? NGX_OK : NGX_FAIL;
}

API NVSDK_NGX_D3D12_DestroyParameters(struct params *p)
{
    ngx_log("%s P%d", __func__, p ? p->id : -1);
    params_free(p);
    return NGX_OK;
}

API NVSDK_NGX_D3D12_GetScratchBufferSize(int feature, const struct params *p, size_t *out)
{
    (void)p;
    ngx_log("%s feature=%d", __func__, feature);
    if (out)
        *out = 0;
    return NGX_OK;
}

API NVSDK_NGX_D3D12_GetFeatureRequirements(void *adapter, const void *discovery, void *req_)
{
    (void)adapter; (void)discovery;
    ngx_log("%s", __func__);
    if (req_) {
        unsigned *req = req_;
        req[0] = 0;   /* FeatureSupported = Supported */
        req[1] = 0;   /* MinHWArchitecture */
        ((char *)req_)[8] = 0;
    }
    return NGX_OK;
}

API NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList *cl, int feature, struct params *p, struct feature **out)
{
    (void)cl;
    ngx_log("%s feature=%d P%d", __func__, feature, p ? p->id : -1);
    if (!out || !p)
        return NGX_FAIL_INVALID_PARAMETER;
    if (feature != FEATURE_SUPERSAMPLING) {
        ngx_log("  -> not supported");
        return NGX_FAIL_FEATURE_NOT_SUPPORTED;
    }
    struct feature *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *f);
    if (!f)
        return NGX_FAIL;
    f->Id = (unsigned)InterlockedIncrement(&g_feature_ids);
    f->kind = feature;
    f->quality = 2;
    peek_ui(p, "Width", &f->render_w);
    peek_ui(p, "Height", &f->render_h);
    peek_ui(p, "OutWidth", &f->out_w);
    peek_ui(p, "OutHeight", &f->out_h);
    peek_i(p, "DLSS.Feature.Create.Flags", &f->flags);
    peek_i(p, "PerfQualityValue", &f->quality);
    ngx_log("  handle %u: render %ux%u -> output %ux%u quality=%d", f->Id, f->render_w, f->render_h, f->out_w, f->out_h, f->quality);
    log_flags(f->flags);
    /* max render = output size, so dynamic resolution up to DLAA fits */
    struct fsr_create_info ci = {
        .max_render_w = f->out_w ? f->out_w : f->render_w,
        .max_render_h = f->out_h ? f->out_h : f->render_h,
        .out_w = f->out_w, .out_h = f->out_h, .ngx_flags = f->flags,
    };
    f->fsr = fsr_create(g_device, &ci);
    if (!f->fsr && g_device) {
        /* a real game: refuse, so it falls back instead of showing garbage */
        HeapFree(GetProcessHeap(), 0, f);
        ngx_log("  -> FSR unavailable, CreateFeature fails");
        return NGX_FAIL | 11; /* UnableToInitializeFeature */
    }
    *out = f;
    return NGX_OK;
}

API NVSDK_NGX_D3D12_ReleaseFeature(struct feature *f)
{
    ngx_log("%s handle=%u evals=%u", __func__, f ? f->Id : 0, f ? f->evals : 0);
    if (f) {
        fsr_destroy(f->fsr);
        HeapFree(GetProcessHeap(), 0, f);
    }
    return NGX_OK;
}

static unsigned evaluate(const char *fn, ID3D12GraphicsCommandList *cl, struct feature *f, struct params *p)
{
    if (!f || !p)
        return NGX_FAIL_INVALID_PARAMETER;
    unsigned n = ++f->evals;
    InterlockedIncrement(&g_total_evals);
    /* full detail for the first frames, then one line every 600 */
    int detail = n <= 3;
    if (detail || n % 600 == 0) {
        float jx = 0, jy = 0, mx = 1, my = 1, pre = 1, dt = 0;
        int reset = 0;
        unsigned sw = 0, sh = 0;
        peek_f(p, "Jitter.Offset.X", &jx);
        peek_f(p, "Jitter.Offset.Y", &jy);
        peek_f(p, "MV.Scale.X", &mx);
        peek_f(p, "MV.Scale.Y", &my);
        peek_f(p, "DLSS.Pre.Exposure", &pre);
        peek_f(p, "FrameTimeDeltaInMsec", &dt);
        peek_i(p, "Reset", &reset);
        peek_ui(p, "DLSS.Render.Subrect.Dimensions.Width", &sw);
        peek_ui(p, "DLSS.Render.Subrect.Dimensions.Height", &sh);
        ngx_log("%s handle=%u #%u cmdlist=%p jitter=(%.3f,%.3f) mvscale=(%g,%g) reset=%d subrect=%ux%u preexp=%g dt=%gms",
                fn, f->Id, n, (void *)cl, jx, jy, mx, my, reset, sw, sh, pre, dt);
        if (detail) {
            log_resource("Color", peek_p(p, "Color"));
            log_resource("Output", peek_p(p, "Output"));
            log_resource("Depth", peek_p(p, "Depth"));
            log_resource("MotionVectors", peek_p(p, "MotionVectors"));
            log_resource("ExposureTexture", peek_p(p, "ExposureTexture"));
            log_resource("TransparencyMask", peek_p(p, "TransparencyMask"));
            log_resource("BiasCurrentColorMask", peek_p(p, "DLSS.Input.Bias.Current.Color.Mask"));
        }
    }
    if (!f->fsr)
        return NGX_OK; /* logging-only mode (no device) */
    struct fsr_eval_info e = {
        .color = peek_p(p, "Color"), .depth = peek_p(p, "Depth"),
        .motion = peek_p(p, "MotionVectors"), .exposure = peek_p(p, "ExposureTexture"),
        .transparency = NULL, .reactive = NULL, .output = peek_p(p, "Output"),
        .mv_scale_x = 1, .mv_scale_y = 1, .pre_exposure = 1,
        .render_w = f->render_w, .render_h = f->render_h, .out_w = f->out_w, .out_h = f->out_h,
    };
    peek_f(p, "Jitter.Offset.X", &e.jitter_x);
    peek_f(p, "Jitter.Offset.Y", &e.jitter_y);
    peek_f(p, "MV.Scale.X", &e.mv_scale_x);
    peek_f(p, "MV.Scale.Y", &e.mv_scale_y);
    peek_f(p, "DLSS.Pre.Exposure", &e.pre_exposure);
    peek_f(p, "FrameTimeDeltaInMsec", &e.frame_ms);
    peek_f(p, "Sharpness", &e.sharpness);
    peek_i(p, "Reset", &e.reset);
    unsigned sw = 0, sh = 0;
    if (peek_ui(p, "DLSS.Render.Subrect.Dimensions.Width", &sw) && sw &&
        peek_ui(p, "DLSS.Render.Subrect.Dimensions.Height", &sh) && sh) {
        e.render_w = sw;
        e.render_h = sh;
    }
    if (!fsr_dispatch(f->fsr, cl, &e))
        return NGX_FAIL;
    return NGX_OK;
}

API NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList *cl, struct feature *f, struct params *p, void *cb)
{
    (void)cb;
    return evaluate(__func__, cl, f, p);
}

API NVSDK_NGX_D3D12_EvaluateFeature_C(ID3D12GraphicsCommandList *cl, struct feature *f, struct params *p, void *cb)
{
    (void)cb;
    return evaluate(__func__, cl, f, p);
}

API NVSDK_NGX_UpdateFeature(const void *app, int feature)
{
    (void)app;
    ngx_log("%s feature=%d", __func__, feature);
    return NGX_OK;
}

/* Newer loaders (Streamline's sl.common) look these up too. */
API NVSDK_NGX_D3D12_Init_ProjectID(const char *project, int engine, const char *engine_ver, const wchar_t *path, ID3D12Device *dev, int ver, const void *info)
{
    return NVSDK_NGX_D3D12_Init_with_ProjectID(project, engine, engine_ver, path, dev, ver, info);
}

API NVSDK_NGX_D3D12_PopulateParameters_Impl(struct params *p)
{
    ngx_log("%s P%d", __func__, p ? p->id : -1);
    if (!p)
        return NGX_FAIL_INVALID_PARAMETER;
    fill_caps(p);
    return NGX_OK;
}

API NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl(ID3D12Device *dev, struct params *p)
{
    ngx_log("%s device=%p P%d", __func__, (void *)dev, p ? p->id : -1);
    if (!p)
        return NGX_FAIL_INVALID_PARAMETER;
    fill_caps(p);
    return NGX_OK;
}

/* callbacks the caller registers: accept and ignore */
#define ACCEPT(name) \
    API name(void) { ngx_log("%s (ignored)", #name); return NGX_OK; }
ACCEPT(NVSDK_NGX_SetInfoCallback)
ACCEPT(NVSDK_NGX_SetOverrideStatusCallback)
ACCEPT(NVSDK_NGX_SetTelemetryEvaluateCallback)

/* signatures not in the public headers: decline rather than guess */
#define DECLINE(name) \
    API name(void) { ngx_log("%s -> not implemented", #name); return NGX_FAIL | 18; }
DECLINE(NVSDK_NGX_GetAPIVersion)
DECLINE(NVSDK_NGX_GetApplicationId)
DECLINE(NVSDK_NGX_GetDriverVersion)
DECLINE(NVSDK_NGX_GetDriverVersionEx)
DECLINE(NVSDK_NGX_GetGPUArchitecture)
DECLINE(NVSDK_NGX_GetSnippetVersion)
DECLINE(NVSDK_NGX_ProcessCommand)
DECLINE(NVSDK_NGX_D3D12_DLSSG_GetCurrentSettingsCallback_Impl)
DECLINE(NVSDK_NGX_DirectSR_Init)
DECLINE(NVSDK_NGX_DirectSR_Query)
DECLINE(NVSDK_NGX_DirectSR_Create)
DECLINE(NVSDK_NGX_DirectSR_Execute)
DECLINE(NVSDK_NGX_DirectSR_Destroy)

/* ---- exports: the C wrappers around the parameter object ---- */
#define PEXP __declspec(dllexport) void __cdecl
#define GEXP __declspec(dllexport) unsigned __cdecl
PEXP NVSDK_NGX_Parameter_SetULL(struct params *p, const char *k, unsigned long long v) { if (p) s_ull(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetF(struct params *p, const char *k, float v) { if (p) s_f(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetD(struct params *p, const char *k, double v) { if (p) s_d(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetUI(struct params *p, const char *k, unsigned v) { if (p) s_ui(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetI(struct params *p, const char *k, int v) { if (p) s_i(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetD3d11Resource(struct params *p, const char *k, void *v) { if (p) s_d11(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetD3d12Resource(struct params *p, const char *k, void *v) { if (p) s_d12(p, k, v); }
PEXP NVSDK_NGX_Parameter_SetVoidPointer(struct params *p, const char *k, void *v) { if (p) s_void(p, k, v); }
GEXP NVSDK_NGX_Parameter_GetULL(struct params *p, const char *k, unsigned long long *o) { return p ? g_ull(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetF(struct params *p, const char *k, float *o) { return p ? g_f(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetD(struct params *p, const char *k, double *o) { return p ? g_d(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetUI(struct params *p, const char *k, unsigned *o) { return p ? g_ui(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetI(struct params *p, const char *k, int *o) { return p ? g_i(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetD3d11Resource(struct params *p, const char *k, void **o) { return p ? g_d11(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetD3d12Resource(struct params *p, const char *k, void **o) { return p ? g_d12(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }
GEXP NVSDK_NGX_Parameter_GetVoidPointer(struct params *p, const char *k, void **o) { return p ? g_void(p, k, o) : NGX_FAIL_INVALID_PARAMETER; }

/* ---- other APIs: refuse cleanly so a game falls back instead of crashing ---- */
#define REFUSE(name) \
    API name(void) { ngx_log("%s -> platform not supported", #name); return NGX_FAIL_PLATFORM_ERROR; }
REFUSE(NVSDK_NGX_D3D11_Init)
REFUSE(NVSDK_NGX_D3D11_Init_Ext)
REFUSE(NVSDK_NGX_D3D11_Init_with_ProjectID)
REFUSE(NVSDK_NGX_D3D11_Shutdown)
REFUSE(NVSDK_NGX_D3D11_Shutdown1)
REFUSE(NVSDK_NGX_D3D11_GetParameters)
REFUSE(NVSDK_NGX_D3D11_GetCapabilityParameters)
REFUSE(NVSDK_NGX_D3D11_AllocateParameters)
REFUSE(NVSDK_NGX_D3D11_DestroyParameters)
REFUSE(NVSDK_NGX_D3D11_GetScratchBufferSize)
REFUSE(NVSDK_NGX_D3D11_GetFeatureRequirements)
REFUSE(NVSDK_NGX_D3D11_CreateFeature)
REFUSE(NVSDK_NGX_D3D11_ReleaseFeature)
REFUSE(NVSDK_NGX_D3D11_EvaluateFeature)
REFUSE(NVSDK_NGX_D3D11_EvaluateFeature_C)
REFUSE(NVSDK_NGX_CUDA_Init)
REFUSE(NVSDK_NGX_CUDA_Init_Ext)
REFUSE(NVSDK_NGX_CUDA_Init_with_ProjectID)
REFUSE(NVSDK_NGX_CUDA_Shutdown)
REFUSE(NVSDK_NGX_CUDA_GetParameters)
REFUSE(NVSDK_NGX_CUDA_GetCapabilityParameters)
REFUSE(NVSDK_NGX_CUDA_AllocateParameters)
REFUSE(NVSDK_NGX_CUDA_DestroyParameters)
REFUSE(NVSDK_NGX_CUDA_GetScratchBufferSize)
REFUSE(NVSDK_NGX_CUDA_GetFeatureRequirements)
REFUSE(NVSDK_NGX_CUDA_CreateFeature)
REFUSE(NVSDK_NGX_CUDA_ReleaseFeature)
REFUSE(NVSDK_NGX_CUDA_EvaluateFeature)
REFUSE(NVSDK_NGX_CUDA_EvaluateFeature_C)

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_log_cs);
        char q[8];
        if (GetEnvironmentVariableA("NGX_FSR4_QUIET", q, sizeof q) && q[0] == '1')
            g_verbose_sets = 0;
        log_open(inst);
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        ngx_log("=== ngx-fsr4 " NGX_FSR4_VERSION " loaded into %s (pid %lu)", exe, GetCurrentProcessId());
    } else if (reason == DLL_PROCESS_DETACH) {
        ngx_log("=== unloading");
        if (g_log)
            fclose(g_log);
    }
    return TRUE;
}
