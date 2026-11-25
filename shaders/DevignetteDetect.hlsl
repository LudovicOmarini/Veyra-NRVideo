// Devignette, step 1 of 3: per-frame HUD evidence (GTA 6).
// One dispatch of 256 x 35 threads (groups of 64); the thread's state cell says
// which job it runs. Every job reads only the frame and its own previous cell,
// so the jobs are independent within the dispatch:
//   row 0, x 0..255   is the left HUD margin a sharp edge on this row? (smoothed)
//   row 2, x 0        mini-map score with hysteresis, and the letterbox flag
//   rows 3..34, x<64  white text with a dark outline in the subtitle band
// `reset` (seek, new source, first frame, HUD detection switched back on)
// replaces the history with this frame's evidence instead of blending into it,
// so a still image or the first frame of an export is protected at once.
#include "Devignette.hlsli"

cbuffer Params : register(b0) {
    uint width; uint height; float workingToScene; float reset;
    float mapThreshold; float pad0; float pad1; float pad2;
};
Texture2D<float4> frame : register(t0);
RWTexture2D<float> state : register(u0);

float Luma(float2 uv) { return DvLuma(frame, uint2(width, height), workingToScene, uv); }
float Edge(float2 uv, float2 n) { return DvEdge(frame, uint2(width, height), workingToScene, uv, n); }

// Cinematic: black bars at the top and bottom; no game HUD, subtitles only.
float Letterbox()
{
    float top = 0.0, bot = 0.0, mid = 0.0;
    [loop] for (int i = 0; i < 32; i++)
    {
        const float u = (float(i) + 0.5) / 32.0;
        top += Luma(float2(u, 0.01)) + Luma(float2(u, 0.03));
        bot += Luma(float2(u, 0.97)) + Luma(float2(u, 0.99));
        mid += Luma(float2(u, 0.4)) + Luma(float2(u, 0.6));
    }
    return (top / 64.0 < 10.0 && bot / 64.0 < 10.0 && mid / 64.0 > 25.0) ? 1.0 : 0.0;
}

// Mini-map frame: score = mean of the two sharpest of its left, right and top sides.
float MapScore()
{
    const float4 e = kDvMapEdges;
    float l = 0.0, r = 0.0, t = 0.0;
    [loop] for (int k = 0; k < 16; k++)
    {
        const float f = 0.1 + 0.8 * (float(k) + 0.5) / 16.0;
        const float v = e.y + f * (e.w - e.y);
        const float u = e.x + f * (e.z - e.x);
        l += Edge(float2(e.x, v), float2(-1.0, 0.0));
        r += Edge(float2(e.z, v), float2(1.0, 0.0));
        t += Edge(float2(u, e.y), float2(0.0, -1.0));
    }
    l /= 16.0; r /= 16.0; t /= 16.0;
    return (l + r + t - min(l, min(r, t))) * 0.5;
}

float TextCell(uint cx, uint cy)
{
    const float2 px = 1.0 / float2(width, height);
    const float rad = max(1.0, floor(float(height) / 540.0 + 0.5));
    float hits = 0.0;
    [loop] for (int sy = 0; sy < 2; sy++)
    [loop] for (int sx = 0; sx < 3; sx++)
    {
        const float2 p = float2(0.2 + 0.6 * (float(cx) + (float(sx) + 0.5) / 3.0) / float(kDvTextWidth),
                                0.8 + 0.16 * (float(cy) + (float(sy) + 0.5) / 2.0) / float(kDvTextHeight));
        const float3 c = saturate(DvFrame(frame, uint2(width, height), workingToScene, p));
        const float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
        if (mx < 200.0 / 255.0 || mx - mn > 40.0 / 255.0) continue;
        float dark = 0.0;
        [loop] for (int k = 1; k <= 4; k++)
        {
            if (float(k) > rad + 1.0) break;
            const float fk = float(k);
            dark = max(dark, step(Luma(p + float2(fk, 0.0) * px), 70.0));
            dark = max(dark, step(Luma(p + float2(-fk, 0.0) * px), 70.0));
            dark = max(dark, step(Luma(p + float2(0.0, fk) * px), 70.0));
            dark = max(dark, step(Luma(p + float2(0.0, -fk) * px), 70.0));
        }
        hits += dark;   // bright, barely saturated pixel with a dark outline
    }
    return hits / 6.0;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const bool first = reset > 0.5;
    if (id.y == kDvRowSmooth && id.x < kDvRows)
    {
        // Boxes aligned on the left margin (help, place or vehicle name, objectives).
        // An edge that does not hold over time fades out. In a cinematic there are no game boxes.
        const float hit = Letterbox() > 0.5 ? 0.0
            : Edge(float2(kDvLeftMargin, (float(id.x) + 0.5) / float(kDvRows)), float2(-1.0, 0.0));
        const float prev = first ? hit : state[id.xy];
        state[id.xy] = lerp(prev, hit, 0.3);
    }
    else if (id.y == kDvRowScalars && id.x == 0)
    {
        // Hysteresis: on at or above the threshold, off under threshold - 0.15.
        const float cine = Letterbox();
        const float score = cine > 0.5 ? 0.0 : MapScore();
        const float prev = first ? 0.0 : state[uint2(0, kDvRowScalars)];   // history is undefined on reset
        const float target = score >= mapThreshold ? 1.0 : (score < mapThreshold - 0.15 ? 0.0 : step(0.5, prev));
        state[uint2(0, kDvRowScalars)] = first ? target : lerp(prev, target, 0.25);
        state[uint2(1, kDvRowScalars)] = cine;
    }
    else if (id.y >= kDvRowText && id.y < kDvRowText + kDvTextHeight && id.x < kDvTextWidth)
    {
        state[id.xy] = TextCell(id.x, id.y - kDvRowText);
    }
}
