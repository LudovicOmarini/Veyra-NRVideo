// Devignette (去暗角): undo a vignette the game baked into its picture, while the
// HUD it drew afterwards (GTA 6 mini-map, left-aligned boxes, subtitles) is left
// as it is. Port of the Dévignetteur ReShade shader (Ludovic Omarini).
//
// The vignette model and every detection threshold were measured on 8-bit
// sRGB-encoded game captures, so detection and correction both run on encoded
// values here: the working texture is linear (1.0 = SDR white after
// workingToScene) and is encoded on read and decoded on write.
//
// Persistent detection state lives in one R32_FLOAT texture (typed UAV loads of
// R32_FLOAT are guaranteed on every D3D12 device):
//   row 0           x 0..255  left-margin edge found on that row (smoothed)
//   row 1           x 0..255  row covered by a left-aligned box (0..1)
//   row 2           x 0 mini-map presence (smoothed), x 1 letterbox (cinematic),
//                   x 2 subtitle presence (smoothed), x 3..6 subtitle box l,t,r,b
//   rows 3..34      x 0..63   white outlined text density in the subtitle band
#ifndef VEYRA_DEVIGNETTE_HLSLI
#define VEYRA_DEVIGNETTE_HLSLI

static const uint kDvStateWidth = 256;
static const uint kDvStateHeight = 35;
static const uint kDvRows = 256;
static const uint kDvRowSmooth = 0;
static const uint kDvRowMask = 1;
static const uint kDvRowScalars = 2;
static const uint kDvRowText = 3;
static const uint kDvTextWidth = 64;
static const uint kDvTextHeight = 32;

// GTA 6 HUD geometry in a 16:9 frame (fractions of width / height).
static const float4 kDvMapEdges = float4(0.01641, 0.81759, 0.17275, 0.97037); // left, top, right, bottom
static const float4 kDvMapRect = float4(0.006, 0.77, 0.18, 0.99);              // map + place name above it
static const float kDvLeftMargin = 0.01641;                                    // 31.5 px at 1080p
static const float kDvFeather = 0.004;
static const float kDvGainFloor = 0.05;

float3 DvEncode(float3 c)
{
    // sRGB OETF, extended above 1.0 with the same power segment (HDR highlights).
    // lerp/step instead of a vector ternary: valid in both HLSL 2018 and 2021.
    c = max(c, 0.0);
    return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}

float3 DvDecode(float3 e)
{
    e = max(e, 0.0);
    return lerp(e / 12.92, pow((e + 0.055) / 1.055, 2.4), step(0.04045, e));
}

// Encoded colour of one frame pixel at a normalised position (point sampled,
// clamped at the edges like the ReShade back buffer sampler).
float3 DvFrame(Texture2D<float4> frame, uint2 size, float workingToScene, float2 uv)
{
    const int2 p = clamp(int2(floor(uv * float2(size))), int2(0, 0), int2(size) - 1);
    return DvEncode(frame[p].rgb * workingToScene);
}

// Luma on the 0..255 scale every threshold below was measured on.
float DvLuma(Texture2D<float4> frame, uint2 size, float workingToScene, float2 uv)
{
    return dot(saturate(DvFrame(frame, size, workingToScene, uv)), float3(0.2126, 0.7152, 0.0722)) * 255.0;
}

// A sharp edge must fall exactly (±1 px) where it is tested, clearly stronger
// than the luminance variation around it. n is the outward normal.
float DvEdge(Texture2D<float4> frame, uint2 size, float workingToScene, float2 uv, float2 n)
{
    const float2 st = n / float2(size) * max(1.0, float(size.y) / 1080.0);
    float peak = 0.0, side = 0.0, sideMax = 0.0;
    [loop] for (int o = -8; o <= 8; o++)
    {
        const float g = abs(DvLuma(frame, size, workingToScene, uv + st * float(o + 1)) -
                            DvLuma(frame, size, workingToScene, uv + st * float(o - 1)));
        if (abs(o) <= 1) peak = max(peak, g);
        else if (abs(o) >= 5) { side += g; sideMax = max(sideMax, g); }
    }
    const float ref = (side - sideMax) / 7.0;
    return (peak >= 6.0 && peak >= 2.0 * ref + 3.0) ? 1.0 : 0.0;
}

// Rectangle with softened borders (l, t, r, b in normalised coordinates).
float DvRect(float2 uv, float4 b)
{
    const float fe = kDvFeather;
    return smoothstep(b.x - fe, b.x + fe, uv.x) * (1.0 - smoothstep(b.z - fe, b.z + fe, uv.x)) *
           smoothstep(b.y - fe, b.y + fe, uv.y) * (1.0 - smoothstep(b.w - fe, b.w + fe, uv.y));
}

#endif
