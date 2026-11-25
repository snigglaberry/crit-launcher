// Crit Launcher Native — HLSL for the DirectX 11 renderer (compiled at runtime by d3dcompiler_47).
#pragma once

namespace crit {

static const char* kShaderSource = R"HLSL(
struct VSIn {
    float2 pos   : POS;
    float2 uv    : UVC;
    float4 color : COL;
    float2 local : LOCAL;
    float2 ext   : EXT;
    float2 rp    : RP;
};

struct PSIn {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    float2 local : TEXCOORD1;
    float2 ext   : TEXCOORD2;
    float2 rp    : TEXCOORD3;
};

cbuffer Params : register(b0) {
    float4 g_params;  // x = shape type (0 fill, 1 shadow, 2 border)
};

Texture2D    g_tex : register(t0);
SamplerState g_smp : register(s0);

PSIn VSMain(VSIn i) {
    PSIn o;
    o.pos = float4(i.pos, 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    o.local = i.local;
    o.ext = i.ext;
    o.rp = i.rp;
    return o;
}

float4 PSImage(PSIn i) : SV_Target {
    float4 c = g_tex.Sample(g_smp, i.uv);
    return float4(c.rgb * i.color.rgb, c.a * i.color.a);
}

float4 PSText(PSIn i) : SV_Target {
    float a = g_tex.Sample(g_smp, i.uv).a;
    return float4(i.color.rgb, i.color.a * a);
}

// exact sdf for a rounded box centered on the pixel
float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 PSShape(PSIn i) : SV_Target {
    float2 p = (i.local * 2.0 - 1.0) * i.ext;
    float d = sdRoundBox(p, i.ext, i.rp.x);
    float cov;
    if (g_params.x < 0.5) {
        cov = saturate(0.5 - d);
    } else if (g_params.x < 1.5) {
        // shadow: gaussian falloff from the shape edge
        float blur = max(i.rp.y, 0.0001);
        float t = saturate(-d / blur + 0.35);
        cov = t * t * (3.0 - 2.0 * t);
    } else {
        float w = max(i.rp.y, 0.5);
        cov = saturate(0.5 - (abs(d + w * 0.5) - w * 0.5));
    }
    return float4(i.color.rgb, i.color.a * cov);
}
)HLSL";

}  // namespace crit
