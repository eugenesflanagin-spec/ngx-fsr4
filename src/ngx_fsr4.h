#pragma once
/* shared between ngx.c (C) and fsr.cpp (the FSR backend) */
#include <d3d12.h>

#ifdef __cplusplus
extern "C" {
#endif

void ngx_log(const char *fmt, ...);

struct fsr_create_info {
    unsigned max_render_w, max_render_h, out_w, out_h;
    int ngx_flags; /* DLSS.Feature.Create.Flags */
};

struct fsr_eval_info {
    ID3D12Resource *color, *depth, *motion, *exposure, *reactive, *transparency, *output;
    float jitter_x, jitter_y, mv_scale_x, mv_scale_y;
    unsigned render_w, render_h, out_w, out_h;
    float sharpness, frame_ms, pre_exposure;
    int reset;
};

void *fsr_create(ID3D12Device *dev, const struct fsr_create_info *ci);
int fsr_dispatch(void *fsr, ID3D12GraphicsCommandList *cl, const struct fsr_eval_info *e);
void fsr_destroy(void *fsr);

#ifdef __cplusplus
}
#endif
