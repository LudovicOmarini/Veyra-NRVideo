// Devignette, step 2 of 3: turn the evidence of step 1 into HUD regions.
// One dispatch of 5 groups of 64 threads:
//   x 0..255  row 1: a row belongs to a box when a continuous left-margin edge
//             segment (~25 px at 1080p) passes near it;
//   x 256     row 2, cells 2..6: subtitle box from the text density grid.
// The frame (t0) is bound by the shared root signature but not read here.
#include "Devignette.hlsli"

cbuffer Params : register(b0) {
    uint width; uint height; float workingToScene; float reset;
    float mapThreshold; float pad0; float pad1; float pad2;
};
Texture2D<float4> frame : register(t0);
RWTexture2D<float> state : register(u0);

float Smoothed(int row) { return state[uint2(clamp(row, 0, int(kDvRows) - 1), kDvRowSmooth)]; }
float Text(uint x, uint y) { return state[uint2(x, kDvRowText + y)]; }

void Subtitles()
{
    float total = 0.0, rows = 0.0, y0 = 1.0, y1 = 0.0;
    [loop] for (uint y = 0; y < kDvTextHeight; y++)
    {
        float s = 0.0;
        [loop] for (uint x = 0; x < kDvTextWidth; x++) s += Text(x, y);
        total += s;
        if (s / float(kDvTextWidth) >= 0.01)
        {
            rows += 1.0;
            const float v = 0.8 + 0.16 * (float(y) + 0.5) / float(kDvTextHeight);
            y0 = min(y0, v); y1 = max(y1, v);
        }
    }
    // Horizontal extent: 2 % and 98 % of the text mass.
    float acc = 0.0, x0 = 0.2, x1 = 0.8, f0 = 0.0, f1 = 0.0;
    [loop] for (uint x = 0; x < kDvTextWidth; x++)
    {
        float s = 0.0;
        [loop] for (uint y = 0; y < kDvTextHeight; y++) s += Text(x, y);
        acc += s;
        const float u = 0.2 + 0.6 * (float(x) + 0.5) / float(kDvTextWidth);
        if (f0 < 0.5 && acc >= 0.02 * total) { x0 = u; f0 = 1.0; }
        if (f1 < 0.5 && acc >= 0.98 * total) { x1 = u; f1 = 1.0; }
    }
    const float present = (rows >= 2.0 && total >= 6.0) ? 1.0 : 0.0;
    const bool first = reset > 0.5;
    const float prevPres = first ? 0.0 : state[uint2(2, kDvRowScalars)];
    const float4 prevRect = first ? float4(0.0, 0.0, 0.0, 0.0)
        : float4(state[uint2(3, kDvRowScalars)], state[uint2(4, kDvRowScalars)],
                 state[uint2(5, kDvRowScalars)], state[uint2(6, kDvRowScalars)]);
    const float pres = first ? present : lerp(prevPres, present, 0.35);
    float4 rect = present > 0.5 ? float4(x0 - 0.02, y0 - 0.015, x1 + 0.02, y1 + 0.015) : prevRect;
    if (present > 0.5 && prevPres > 0.3) rect = lerp(prevRect, rect, 0.5);
    state[uint2(2, kDvRowScalars)] = pres;
    state[uint2(3, kDvRowScalars)] = rect.x;
    state[uint2(4, kDvRowScalars)] = rect.y;
    state[uint2(5, kDvRowScalars)] = rect.z;
    state[uint2(6, kDvRowScalars)] = rect.w;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < kDvRows)
    {
        float m = 0.0;
        [loop] for (int k = -6; k <= 6; k++)
        {
            float s = 0.0;
            [loop] for (int j = -3; j <= 3; j++) s += Smoothed(int(id.x) + k + j);
            m = max(m, step(0.55, s / 7.0));
        }
        state[uint2(id.x, kDvRowMask)] = m;
    }
    else if (id.x == kDvRows)
    {
        Subtitles();
    }
}
