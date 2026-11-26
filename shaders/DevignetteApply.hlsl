// Devignette, step 3 of 3: per-pixel correction.
// Vignette model on the encoded picture: V(r) = 1 - strength * r^falloff, with
// r = 0 at the centre and 1 in the corners (circle: aspect-corrected; ellipse:
// follows the frame; rectangle: a superellipse of exponent `power`). The centre
// can be moved, the shape stretched towards the sides or the top and bottom,
// and the gradient can start away from the centre. The gain 1/V is
// reduced on highlights (protect) and removed on the HUD the game drew after its vignette (mask from steps 1-2 plus
// an optional fixed region). SDR keeps the soft shoulder of the original
// filter; HDR keeps values above diffuse white instead of compressing them.
#include "Devignette.hlsli"

cbuffer Params : register(b0) {
    uint width; uint height; float workingToScene; float strength;
    float falloff; float protect; float shape; float hudAuto;
    float maskOn; float maskLeft; float maskTop; float maskRight;
    float maskBottom; float hdrWorking; float centerY; float centerX;
    float stretch; float start; float power; float pad0;
};
Texture2D<float4> source : register(t0);
Texture2D<float> state : register(t1);
RWTexture2D<float4> target : register(u0);

float Scalar(uint x) { return state[uint2(x, kDvRowScalars)]; }

float RowsMask(float v)
{
    // Bilinear between the 256 row centres, as the original's linear sampler did.
    const float t = v * float(kDvRows) - 0.5;
    const int i0 = clamp(int(floor(t)), 0, int(kDvRows) - 1);
    const int i1 = min(i0 + 1, int(kDvRows) - 1);
    return lerp(state[uint2(i0, kDvRowMask)], state[uint2(i1, kDvRowMask)], saturate(t - floor(t)));
}

float HudMask(float2 uv)
{
    float m = maskOn > 0.5 ? DvRect(uv, float4(maskLeft, maskTop, maskRight, maskBottom)) : 0.0;
    if (hudAuto < 0.5) return m;
    m = max(m, DvRect(uv, kDvMapRect) * Scalar(0));
    if (uv.x < 0.27)
        m = max(m, RowsMask(uv.y) * smoothstep(0.006, 0.012, uv.x) * (1.0 - smoothstep(0.255, 0.265, uv.x)));
    if (uv.y > 0.75)
        m = max(m, DvRect(uv, float4(Scalar(3), Scalar(4), Scalar(5), Scalar(6))) * smoothstep(0.4, 0.6, Scalar(2)));
    return m;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    const float4 original = source[id.xy];
    const float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    const float aspect = float(width) / float(height);
    const float3 col = DvEncode(original.rgb * workingToScene);

    // The radius keeps the centred normalisation (1 = a corner of a centred,
    // unstretched vignette), so the neutral geometry is the original model and
    // the far corners of a shifted or stretched one may reach past 1.
    const float2 d = uv - float2(0.5 + centerX, 0.5 + centerY);
    float2 q = d / 0.5;   // -1..1 across the frame
    if (shape < 0.5) q *= float2(aspect, 1.0) * (1.41421356 / length(float2(aspect, 1.0)));
    q = abs(q * float2(exp(stretch), exp(-stretch)));
    // p-norm, divided by its value in a corner (q = (1, 1)): p = 2 is the
    // circle / ellipse, large p tends to a rectangle.
    float r = power == 2.0 ? length(q) / 1.41421356
        : pow(pow(max(q.x, 1e-4), power) + pow(max(q.y, 1e-4), power), 1.0 / power) * exp2(-1.0 / power);
    r = min(max(r - start, 0.0) / (1.0 - start), 1.5);
    const float vignette = 1.0 - strength * pow(max(r, 1e-5), falloff);
    float gain = 1.0 / max(vignette, kDvGainFloor);

    // Highlights: brighten less what is already bright (sun, clouds).
    const float luma = dot(col, float3(0.2126, 0.7152, 0.0722));
    gain = lerp(gain, 1.0, protect * smoothstep(0.5, 1.0, luma));
    // HUD: drawn by the game after the vignette, left untouched.
    gain = lerp(gain, 1.0, HudMask(uv));

    float3 o = col * gain;
    if (hdrWorking < 0.5)
    {
        if (protect > 0.0)
        {
            // Soft shoulder instead of a hard clip at 1.0.
            const float k = 1.0 - 0.5 * protect;
            const float kk = max(1.0 - k, 1e-4);
            const float mx = max(o.r, max(o.g, o.b));
            if (mx > k) o *= (k + kk * (1.0 - exp(-(mx - k) / kk))) / mx;
            o = max(o, col);
        }
        o = saturate(o);
    }
    target[id.xy] = float4(DvDecode(o) / workingToScene, original.a);
}
