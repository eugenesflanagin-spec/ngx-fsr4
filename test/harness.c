/* Drives nvngx.dll the way a game's NGX helper code does, without a GPU:
 * capability query, optimal-settings callback, create, evaluate, release.
 * Usage: harness.exe [path\to\nvngx.dll] */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef unsigned (__cdecl *pfn_init)(unsigned long long, const wchar_t *, void *, int);
typedef unsigned (__cdecl *pfn_pp)(void **);
typedef unsigned (__cdecl *pfn_p)(void *);
typedef unsigned (__cdecl *pfn_create)(void *, int, void *, void **);
typedef unsigned (__cdecl *pfn_eval)(void *, void *, void *, void *);
typedef unsigned (__cdecl *pfn_cb)(void *);

/* MSVC vtable slots of NVSDK_NGX_Parameter, from clang's dump */
typedef void (__cdecl *set_ui_t)(void *, const char *, unsigned);
typedef void (__cdecl *set_i_t)(void *, const char *, int);
typedef void (__cdecl *set_f_t)(void *, const char *, float);
typedef void (__cdecl *set_p_t)(void *, const char *, void *);
typedef unsigned (__cdecl *get_ui_t)(void *, const char *, unsigned *);
typedef unsigned (__cdecl *get_i_t)(void *, const char *, int *);
typedef unsigned (__cdecl *get_f_t)(void *, const char *, float *);
typedef unsigned (__cdecl *get_p_t)(void *, const char *, void **);
enum { SET_VOID, SET_D12, SET_D11, SET_I, SET_UI, SET_D, SET_F, SET_ULL,
       GET_VOID, GET_D12, GET_D11, GET_I, GET_UI, GET_D, GET_F, GET_ULL, RESET };
#define VT(obj, idx, type) ((type)((*(void ***)(obj))[idx]))

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

int main(int argc, char **argv)
{
    const char *dll = argc > 1 ? argv[1] : "nvngx.dll";
    HMODULE m = LoadLibraryA(dll);
    CHECK(m != NULL, "load %s", dll);
    if (!m)
        return 1;
#define SYM(t, n) t n = (t)(void *)GetProcAddress(m, "NVSDK_NGX_D3D12_" #n); CHECK(n != NULL, "export %s", #n)
    SYM(pfn_init, Init);
    SYM(pfn_pp, GetCapabilityParameters);
    SYM(pfn_pp, AllocateParameters);
    SYM(pfn_p, DestroyParameters);
    SYM(pfn_create, CreateFeature);
    SYM(pfn_eval, EvaluateFeature);
    SYM(pfn_p, ReleaseFeature);
    SYM(pfn_p, Shutdown1);
    if (fails)
        return 1;

    CHECK(Init(0x1234, L"C:\\temp", NULL, 0x15) == 1, "Init returns Success (0x1)");

    void *caps = NULL;
    CHECK(GetCapabilityParameters(&caps) == 1 && caps, "GetCapabilityParameters");
    int avail = -1, upd = -1;
    CHECK(VT(caps, GET_I, get_i_t)(caps, "SuperSampling.Available", &avail) == 1 && avail == 1, "SuperSampling.Available = %d", avail);
    CHECK(VT(caps, GET_I, get_i_t)(caps, "#\x01", &avail) == 1 && avail == 1, "E-param #x01 alias = %d", avail);
    CHECK(VT(caps, GET_I, get_i_t)(caps, "SuperSampling.NeedsUpdatedDriver", &upd) == 1 && upd == 0, "NeedsUpdatedDriver = %d", upd);

    /* NGX_DLSS_GET_OPTIMAL_SETTINGS, done through vtable slots */
    void *cb = NULL;
    CHECK(VT(caps, GET_VOID, get_p_t)(caps, "DLSSOptimalSettingsCallback", &cb) == 1 && cb, "optimal settings callback %p", cb);
    VT(caps, SET_UI, set_ui_t)(caps, "Width", 2560);
    VT(caps, SET_UI, set_ui_t)(caps, "Height", 1440);
    VT(caps, SET_I, set_i_t)(caps, "PerfQualityValue", 2);
    unsigned ow = 0, oh = 0, minw = 0;
    float sharp = -1;
    CHECK(((pfn_cb)cb)(caps) == 1, "callback returns Success");
    VT(caps, GET_UI, get_ui_t)(caps, "OutWidth", &ow);
    VT(caps, GET_UI, get_ui_t)(caps, "OutHeight", &oh);
    VT(caps, GET_UI, get_ui_t)(caps, "DLSS.Get.Dynamic.Min.Render.Width", &minw);
    VT(caps, GET_F, get_f_t)(caps, "Sharpness", &sharp);
    CHECK(ow == 1707 && oh == 960, "Quality 2560x1440 -> %ux%u", ow, oh);
    CHECK(minw == 853 && sharp == 0.0f, "min width %u sharpness %g", minw, (double)sharp);

    /* float slot really receives XMM2, double slot too */
    float f = 0;
    VT(caps, SET_F, set_f_t)(caps, "Test.F", 0.625f);
    CHECK(VT(caps, GET_F, get_f_t)(caps, "Test.F", &f) == 1 && f == 0.625f, "float round-trip %g", (double)f);
    int i = 0;
    CHECK(VT(caps, GET_I, get_i_t)(caps, "Test.F", &i) == 1 && i == 0, "float read as int %d", i);
    CHECK(VT(caps, GET_I, get_i_t)(caps, "Nope", &i) != 1, "missing key fails");
    CHECK(VT(caps, GET_VOID, get_p_t)(caps, "Width", &cb) != 1, "number read as pointer refused");

    /* create + evaluate like NGX_D3D12_CREATE_DLSS_EXT / EVALUATE */
    void *cp = NULL, *h = NULL;
    CHECK(AllocateParameters(&cp) == 1 && cp, "AllocateParameters");
    VT(cp, SET_UI, set_ui_t)(cp, "Width", ow);
    VT(cp, SET_UI, set_ui_t)(cp, "Height", oh);
    VT(cp, SET_UI, set_ui_t)(cp, "OutWidth", 2560);
    VT(cp, SET_UI, set_ui_t)(cp, "OutHeight", 1440);
    VT(cp, SET_I, set_i_t)(cp, "PerfQualityValue", 2);
    VT(cp, SET_I, set_i_t)(cp, "DLSS.Feature.Create.Flags", 1 | 2 | 8 | 64);
    CHECK(CreateFeature(NULL, 1, cp, &h) == 1 && h && *(unsigned *)h, "CreateFeature(SuperSampling) handle id %u", h ? *(unsigned *)h : 0);
    void *h2 = NULL;
    CHECK(CreateFeature(NULL, 11, cp, &h2) != 1 && !h2, "CreateFeature(FrameGeneration) refused");
    for (int n = 0; n < 5; n++) {
        VT(cp, SET_F, set_f_t)(cp, "Jitter.Offset.X", 0.25f * n);
        VT(cp, SET_F, set_f_t)(cp, "Jitter.Offset.Y", -0.125f);
        VT(cp, SET_I, set_i_t)(cp, "Reset", n == 0);
        CHECK(EvaluateFeature(NULL, h, cp, NULL) == 1, "EvaluateFeature #%d", n + 1);
    }
    CHECK(ReleaseFeature(h) == 1, "ReleaseFeature");
    CHECK(DestroyParameters(cp) == 1, "DestroyParameters (alloc)");
    CHECK(DestroyParameters(caps) == 1, "DestroyParameters (caps)");
    CHECK(Shutdown1(NULL) == 1, "Shutdown1");

    void (__cdecl *setf)(void *, const char *, float) = (void *)GetProcAddress(m, "NVSDK_NGX_Parameter_SetF");
    CHECK(setf != NULL, "C wrapper NVSDK_NGX_Parameter_SetF exported");

    printf("%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
