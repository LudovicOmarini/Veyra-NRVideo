#include "veyra/pipeline/DevignettePass.h"
#include <algorithm>
#include <bit>

namespace veyra::pipeline {
namespace {
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kStateFormat = DXGI_FORMAT_R32_FLOAT;
// Must match Devignette.hlsli (kDvStateWidth / kDvStateHeight).
constexpr uint32_t kStateWidth = 256, kStateHeight = 35;
}

bool DevignettePass::prepare(ID3D12Device* device, ID3D12Resource* target)
{
    const auto desc = target->GetDesc();
    if (target == target_ && copy_ && uint32_t(desc.Width) == width_ && desc.Height == height_) return true;
    close();
    if (desc.Format != kFormat) {
        veyra::log::warn("devignette", std::format("target format {} is not RGBA16F; devignette skipped", unsigned(desc.Format)));
        return false;
    }
    width_ = uint32_t(desc.Width);
    height_ = desc.Height;
    copy_ = makeTexture(device, width_, height_, kFormat, false);
    state_ = makeTexture(device, kStateWidth, kStateHeight, kStateFormat, true);
    if (!copy_ || !state_) { close(); return false; }
    std::vector<uint8_t> cs;
    // detect / resolve: t0 copy, u0 state. apply: t0 copy, t1 state, u0 target.
    if (!detect_.loadShader("DevignetteDetect.dxil", cs) || !detect_.create(device, cs, 2, 1, 1) ||
        !resolve_.loadShader("DevignetteResolve.dxil", cs) || !resolve_.create(device, cs, 2, 1, 1) ||
        !apply_.loadShader("DevignetteApply.dxil", cs) || !apply_.create(device, cs, 3, 2, 1, 16)) {
        close();
        return false;
    }
    makeSrv(device, copy_.Get(), kFormat, cpuHandleOf(detect_, 0));
    makeUav(device, state_.Get(), kStateFormat, cpuHandleOf(detect_, 1));
    makeSrv(device, copy_.Get(), kFormat, cpuHandleOf(resolve_, 0));
    makeUav(device, state_.Get(), kStateFormat, cpuHandleOf(resolve_, 1));
    makeSrv(device, copy_.Get(), kFormat, cpuHandleOf(apply_, 0));
    makeSrv(device, state_.Get(), kStateFormat, cpuHandleOf(apply_, 1));
    makeUav(device, target, kFormat, cpuHandleOf(apply_, 2));
    target_ = target;
    veyra::log::info("devignette", std::format("devignette pass {}x{} state={}x{}", width_, height_, kStateWidth, kStateHeight));
    return true;
}

bool DevignettePass::run(ID3D12GraphicsCommandList* list, StateTracker& tracker, ID3D12Resource* target,
                         const engine::ColorSettings& settings, bool hdrWorking, bool reset)
{
    if (!wanted(settings)) {
        // Frames went by without detection: the history is stale when it comes back.
        needsReset_ = true;
        return true;
    }
    if (!list || !target || failed_) return true;
    ComPtr<ID3D12Device> device;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !prepare(device.Get(), target)) {
        // The grade itself is already written; the picture continues uncorrected.
        failed_ = true;
        veyra::log::error("devignette", "devignette pass unavailable; continuing without it");
        return false;
    }
    const float workingToScene = hdrWorking ? 80.0f / 203.0f : 1.0f;
    const bool hud = settings.devignetteHudAuto;
    if (hud && !hudWasOn_) needsReset_ = true;   // detection switched back on
    hudWasOn_ = hud;

    tracker.transition(list, target, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tracker.transition(list, copy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(copy_.Get(), target);
    tracker.transition(list, copy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (hud) {
        const bool first = reset || needsReset_;
        tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float detect[8] = {std::bit_cast<float>(width_), std::bit_cast<float>(height_), workingToScene,
            first ? 1.0f : 0.0f, std::clamp(settings.devignetteMapThreshold, 0.3f, 0.9f), 0, 0, 0};
        detect_.bind(list, detect, gpuHandleOf(detect_, 0).ptr, gpuHandleOf(detect_, 1).ptr);
        list->Dispatch(kStateWidth / 64, kStateHeight, 1);
        tracker.uavBarrier(list, state_.Get());
        resolve_.bind(list, detect, gpuHandleOf(resolve_, 0).ptr, gpuHandleOf(resolve_, 1).ptr);
        list->Dispatch(kStateWidth / 64 + 1, 1, 1);
        tracker.uavBarrier(list, state_.Get());
        needsReset_ = false;
    }

    tracker.transition(list, state_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float pct = 0.01f;
    const float apply[16] = {std::bit_cast<float>(width_), std::bit_cast<float>(height_), workingToScene,
        std::clamp(settings.devignetteStrength, 0.0f, 0.95f), std::clamp(settings.devignetteFalloff, 0.5f, 6.0f),
        std::clamp(settings.devignetteProtect, 0.0f, 1.0f),
        settings.devignetteShape == engine::ColorSettings::kDevignetteEllipse ? 1.0f : 0.0f, hud ? 1.0f : 0.0f,
        settings.devignetteMaskOn ? 1.0f : 0.0f,
        std::clamp(settings.devignetteMaskLeft * pct, 0.0f, 1.0f), std::clamp(settings.devignetteMaskTop * pct, 0.0f, 1.0f),
        std::clamp(settings.devignetteMaskRight * pct, 0.0f, 1.0f), std::clamp(settings.devignetteMaskBottom * pct, 0.0f, 1.0f),
        hdrWorking ? 1.0f : 0.0f, std::clamp(settings.devignetteCenterY * pct, -0.25f, 0.25f), 0};
    apply_.bind(list, apply, gpuHandleOf(apply_, 0).ptr, gpuHandleOf(apply_, 2).ptr);
    list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
    tracker.uavBarrier(list, target);
    return true;
}

void DevignettePass::close()
{
    detect_ = {};
    resolve_ = {};
    apply_ = {};
    copy_.Reset();
    state_.Reset();
    target_ = nullptr;
    width_ = height_ = 0;
    needsReset_ = true;
    hudWasOn_ = false;
    failed_ = false;
}

} // namespace veyra::pipeline
