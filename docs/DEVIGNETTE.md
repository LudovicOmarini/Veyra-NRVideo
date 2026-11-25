# 去暗角 · Devignette (Dévignetteur port)

Undoes a vignette that a game baked into its picture, while the HUD the game
drew *after* the vignette is left untouched. Port of the Dévignetteur ReShade
shader by Ludovic Omarini, contributed under this repository's GPL-3.0 licence.

## Where

Colour panel → section **去暗角** (list and node editors). It belongs to the colour
grade: it only runs when the colour node is on and *strength* > 0, so default
settings render exactly as before (no pass, no dispatch).

Order inside a colour node: grade → 纹理/清晰度/去朦胧 → devignette. The values
were measured on the original game picture, so use the first colour node and
leave its other sliders neutral for the most accurate result.

## Parameters (same units as the Dévignetteur page)

| Field | Range | Default | Meaning |
|---|---|---|---|
| `devignetteShape` | circle / ellipse | circle | circle is aspect-corrected, ellipse follows the frame |
| `devignetteStrength` | 0 – 0.95 | 0 (off) | model V(r) = 1 − strength · r^falloff |
| `devignetteFalloff` | 0.5 – 6 | 2 | hardness of the gradient |
| `devignetteProtect` | 0 – 1 | 0.7 | highlight protection + soft shoulder (SDR) |
| `devignetteHudAuto` | on / off | on | detect and skip the GTA 6 HUD |
| `devignetteMapThreshold` | 0.3 – 0.9 | 0.6 | mini-map detection score (hysteresis −0.15) |
| `devignetteMaskOn` + `Left/Top/Right/Bottom` | 0 – 100 % | off, 0.5/77/18/99 | extra fixed region left uncorrected |

**GTA 6 实测暗角** applies the vignette measured on one hour of gameplay:
strength 0.24, falloff 1.45, ellipse (one undo step).

## Implementation

- `include/veyra/pipeline/DevignettePass.h`, `src/pipeline/DevignettePass.cpp`:
  in-place pass on the RGBA16F grade output, three compute steps.
- `shaders/DevignetteDetect.hlsl`: per-frame HUD evidence (left-margin edges,
  mini-map score with hysteresis, letterbox, subtitle text density).
- `shaders/DevignetteResolve.hlsl`: box rows and the subtitle rectangle.
- `shaders/DevignetteApply.hlsl`: per-pixel correction with the HUD mask.
- Detection and correction work on sRGB-encoded values (the thresholds and the
  vignette model were measured on 8-bit captures); the linear working texture is
  encoded on read and decoded on write. HDR keeps values above diffuse white.
- HUD history lives in one 256×35 R32_FLOAT texture (no CPU readback). A
  `reset` (seek, new source, scene cut, first frame) replaces the history with the
  current frame, so a still image or the first exported frame is protected at once.

## Storage

The block is appended to a colour block only by the versions that carry it, and
only when an entry uses it, so older builds keep their files:
PresetStore **v40**, preset library **v27** and mode session **v22** (each in a
`.devignette` sidecar), colour looks **v3**. The export worker protocol is **17**.

## Limits

- HUD detection knows the GTA 6 layout in a 16:9 frame only. For another game,
  turn detection off and use the fixed region.
- Large images processed in tiles (> 16.7 Mpx) skip the devignette with a
  warning: the vignette centre and the HUD are positions in the whole frame.
- The automatic vignette *measurement* of the Dévignetteur page (CPU analysis of
  a still) is not ported; the GTA 6 preset and the sliders replace it.

## Tests

- `veyra_devignette_settings_tests <abs temp dir>`: fields, validation, colour
  block, settings presets v40.
- `veyra_color_look_tests`, `veyra_preset_library_tests`: v3 looks, v27 library
  and v22 session sidecars.
- `veyra_color_grade_gpu_tests`: flat frame — centre unchanged, corners lifted by
  1/(1 − strength·r²), fixed region kept, no HUD found, strength 0 bit-identical.
