#pragma once
#include "veyra/engine/ColorSettings.h"
#include "veyra/pipeline/GpuPassUtils.h"

namespace veyra::pipeline {
// The colour panel's 去暗角 (devignette) group: undoes a vignette the game baked
// into its picture (Dévignetteur, Ludovic Omarini) while the HUD it drew after
// the vignette is detected and left untouched (GTA 6 mini-map, left-aligned
// boxes, subtitles), plus an optional fixed region.
//
// It runs in place on the RGBA16F texture the grade just wrote, after the
// 纹理/清晰度/去朦胧 effects, in three compute steps: HUD evidence (DevignetteDetect),
// HUD regions (DevignetteResolve) and the per-pixel correction (DevignetteApply).
// The HUD detection keeps a small history (one R32_FLOAT texture, see
// Devignette.hlsli) so the mask does not flicker. `reset` - seek, new source,
// first frame - replaces that history with the current frame instead of
// blending, so a still image or the first frame of an export is protected at
// once. Nothing is read back to the CPU.
class DevignettePass {
public:
    static bool wanted(const engine::ColorSettings& settings) {
        return settings.enabled && settings.devignetteStrength > 0.0f;
    }
    // `target` is an RGBA16F texture the grade just wrote; it is read and
    // rewritten in place. `hdrWorking` selects scRGB working units.
    bool run(ID3D12GraphicsCommandList* list, StateTracker& tracker, ID3D12Resource* target,
             const engine::ColorSettings& settings, bool hdrWorking, bool reset);
    void close();

private:
    bool prepare(ID3D12Device* device, ID3D12Resource* target);
    ComputePass detect_, resolve_, apply_;
    ComPtr<ID3D12Resource> copy_, state_;
    ID3D12Resource* target_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    bool needsReset_ = true;   // the state texture holds no valid history yet
    bool hudWasOn_ = false;
    bool failed_ = false;      // one log line, then no retries until close()
};
}
