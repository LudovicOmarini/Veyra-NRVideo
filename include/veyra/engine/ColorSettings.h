#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <istream>
#include <ostream>
#include <string>
namespace veyra::engine {
// Lightroom-aligned colour grade (P1). Every field is neutral at its default
// value, and a default-constructed ColorSettings must leave the frame
// bit-identical: veyra_color_grade_tests asserts that identity path, and the
// graph keeps the stage disabled until something actually changes.
//
// Units follow the Lightroom controls the UI mirrors:
//   temperature/tint, contrast..saturation, mixer, grading, calibration: -100..100
//   exposure: EV (-5..5)     grading hue: 0..360     grading saturation: 0..100
//   curve points: 0..1 normalised input/output
constexpr int kColorMixerBands=8;   // red, orange, yellow, green, aqua, blue, purple, magenta
constexpr int kColorCurveCount=5;   // RGB, red, green, blue, and the parametric helper
constexpr int kColorCurvePoints=8;
constexpr int kColorGradingZones=4; // shadows, midtones, highlights, global

struct ColorCurvePoint { float x=0,y=0; };
struct ColorCurve {
    // Two points are the identity ramp; extra points must stay sorted by x.
    std::array<ColorCurvePoint,kColorCurvePoints> points{};
    int count=2;
    ColorCurve(){points[0]={0,0};points[1]={1,1};}
    bool identity()const{
        return count==2&&points[0].x==0.0f&&points[0].y==0.0f&&points[1].x==1.0f&&points[1].y==1.0f;
    }
    void reset(){*this=ColorCurve{};}
    bool operator==(const ColorCurve& other)const{
        if(count!=other.count)return false;
        for(int i=0;i<count;++i)if(points[i].x!=other.points[i].x||points[i].y!=other.points[i].y)return false;
        return true;
    }
};
struct ColorGradingWheel {
    float hue=0,saturation=0,luminance=0;
    bool operator==(const ColorGradingWheel&)const=default;
};

struct ColorSettings {
    // Master switch (v4 plan): when false the colour stage does not exist at all
    // - no pass, no tables, no dispatch - so users who want the absolute lowest
    // latency pay nothing. Toggling it changes the graph shape (like NR/SR),
    // while changing the parameters below stays a live uniform update.
    bool enabled=false;
    // White balance: relative to the source metadata (capture has no as-shot WB,
    // so the UI must label these as relative values - plan section 5).
    float temperature=0,tint=0;
    // Light.
    float exposure=0,contrast=0,highlights=0,shadows=0,whites=0,blacks=0;
    // Presence. texture/clarity/dehaze are spatial and land in P2; they are part
    // of the model now so schema and UI do not need another format bump.
    float texture=0,clarity=0,dehaze=0,vibrance=0,saturation=0;
    // Tone curve: parametric regions plus five point curves (0 RGB, 1 R, 2 G, 3 B,
    // 4 reserved for the luminance helper used by the UI preview).
    float paramHighlights=0,paramLights=0,paramDarks=0,paramShadows=0;
    float splitHighlights=0,splitMidtones=0,splitShadows=0;
    std::array<ColorCurve,kColorCurveCount> curves{};
    // Colour mixer (HSL) and the black & white mixer.
    std::array<float,kColorMixerBands> mixerHue{};
    std::array<float,kColorMixerBands> mixerSaturation{};
    std::array<float,kColorMixerBands> mixerLuminance{};
    bool blackWhite=false;
    std::array<float,kColorMixerBands> blackWhiteMix{};
    // Colour grading: shadows, midtones, highlights, global.
    std::array<ColorGradingWheel,kColorGradingZones> grading{};
    float gradingBlending=50,gradingBalance=0;
    // Calibration.
    float calibrationShadowTint=0;
    std::array<float,3> calibrationHue{};
    std::array<float,3> calibrationSaturation{};
    // 3D LUT (.cube) reference, stored as the file *name* inside
    // runtime_local/luts - the export job runs in a separate process and
    // resolves the same folder, so an absolute path would be wrong there.
    // Fixed-size POD keeps EnhancementSettings trivially copyable for the
    // shared-memory export header (ExportJobManager static_assert).
    static constexpr int kLutNameChars=260;
    std::array<wchar_t,kLutNameChars> lutName{};
    float lutStrength=100;
    // Input space: 0 = Cineon log (default for creative LUTs), 1 = sRGB display
    // reference, 2 = PQ. The plan forbids silently applying a display-referred
    // LUT to HDR content, so the choice is explicit and logged.
    int lutInputSpace=0;
    // Per-section bypass (plan T3 "分组眼睛"): bit per colour page section -
    // 0 亮, 1 颜色, 2 曲线, 3 混色器, 4 颜色分级, 5 校准, 6 LUT. A bypassed section
    // is baked as neutral, so the user can A/B one group without losing its
    // numbers. Lives in the settings (not the UI) because it changes the pixels.
    uint32_t groupBypassMask=0;
    // 去暗角 (devignette, DevignettePass): undoes a vignette the game baked into
    // its picture. Same parameters and units as the Dévignetteur page: the model
    // is V(r) = 1 - strength * r^falloff on the sRGB-encoded picture (r = 0 at
    // the centre, 1 in the corners). strength 0 is off, so the defaults are
    // neutral. The HUD drawn after the vignette is not corrected: GTA 6 mini-map,
    // left-aligned boxes and subtitles are detected on the GPU (devignetteHudAuto),
    // and a fixed region can be excluded too (percent of the frame).
    // Saved only by the format versions that carry it (see writeColorSettings).
    static constexpr int kDevignetteCircle=0,kDevignetteEllipse=1;
    float devignetteStrength=0;          // 0..0.95
    float devignetteFalloff=2;           // 0.5..6, hardness of the gradient
    // Vertical position of the vignette centre, percent of the frame height
    // (-25..25, positive moves it down): for a vignette stronger at one edge.
    float devignetteCenterY=0;
    float devignetteProtect=0.7f;        // 0..1, highlight protection
    int devignetteShape=kDevignetteCircle;
    bool devignetteHudAuto=true;
    float devignetteMapThreshold=0.6f;   // 0.3..0.9, mini-map detection score
    bool devignetteMaskOn=false;
    float devignetteMaskLeft=0.5f,devignetteMaskTop=77,devignetteMaskRight=18,devignetteMaskBottom=99;

    bool operator==(const ColorSettings&)const=default;
    // True when any devignette field differs from its default: the stores then
    // write the format version that carries the block.
    bool hasDevignette()const{
        const ColorSettings d;
        return devignetteStrength!=d.devignetteStrength||devignetteFalloff!=d.devignetteFalloff||
            devignetteCenterY!=d.devignetteCenterY||
            devignetteProtect!=d.devignetteProtect||devignetteShape!=d.devignetteShape||
            devignetteHudAuto!=d.devignetteHudAuto||devignetteMapThreshold!=d.devignetteMapThreshold||
            devignetteMaskOn!=d.devignetteMaskOn||devignetteMaskLeft!=d.devignetteMaskLeft||
            devignetteMaskTop!=d.devignetteMaskTop||devignetteMaskRight!=d.devignetteMaskRight||
            devignetteMaskBottom!=d.devignetteMaskBottom;
    }
    void resetDevignette(){
        const ColorSettings d;
        devignetteStrength=d.devignetteStrength;devignetteFalloff=d.devignetteFalloff;
        devignetteCenterY=d.devignetteCenterY;
        devignetteProtect=d.devignetteProtect;devignetteShape=d.devignetteShape;
        devignetteHudAuto=d.devignetteHudAuto;devignetteMapThreshold=d.devignetteMapThreshold;
        devignetteMaskOn=d.devignetteMaskOn;devignetteMaskLeft=d.devignetteMaskLeft;
        devignetteMaskTop=d.devignetteMaskTop;devignetteMaskRight=d.devignetteMaskRight;
        devignetteMaskBottom=d.devignetteMaskBottom;
    }
    // GTA 6 vignette measured on one hour of gameplay (Dévignetteur preset).
    void applyGta6Devignette(){
        devignetteStrength=0.24f;devignetteFalloff=1.45f;devignetteShape=kDevignetteEllipse;
    }
    // Neutral means "renders exactly like no grading at all", ignoring the
    // master switch: enabled+neutral must be a visual no-op (verified by test),
    // which lets the stage early-out instead of running an identity transform.
    bool neutral()const{
        auto copy=*this;
        copy.enabled=false;
        return copy==ColorSettings{};
    }

    // P1 knows only these two LUT spaces plus "no LUT"; anything else is a bug.
    static constexpr int kLutInputCineon=0,kLutInputSrgb=1,kLutInputPq=2;

    bool hasLut()const{return lutName[0]!=0;}
    std::wstring lutNameString()const{return std::wstring(lutName.data());}
    // Rejects separators and drive letters: only a bare file name is accepted.
    bool setLutName(std::wstring_view name){
        if(name.size()>=kLutNameChars)return false;
        if(name.find_first_of(L"\\/:*?\"<>|")!=std::wstring_view::npos)return false;
        lutName.fill(0);
        for(std::size_t i=0;i<name.size();++i)lutName[i]=name[i];
        return true;
    }
    void clearLut(){lutName.fill(0);}

    std::string validate()const{
        const auto unit=[](float v){return std::isfinite(v)&&v>=-100.0f&&v<=100.0f;};
        for(float v:{temperature,tint,contrast,highlights,shadows,whites,blacks,texture,clarity,dehaze,vibrance,saturation,
                     paramHighlights,paramLights,paramDarks,paramShadows,splitHighlights,splitMidtones,splitShadows,
                     gradingBalance,calibrationShadowTint})if(!unit(v))return "colour slider out of range";
        for(float v:{exposure})if(!std::isfinite(v)||v<-5.0f||v>5.0f)return "colour exposure out of range";
        for(float v:mixerHue)if(!unit(v))return "mixer hue out of range";
        for(float v:mixerSaturation)if(!unit(v))return "mixer saturation out of range";
        for(float v:mixerLuminance)if(!unit(v))return "mixer luminance out of range";
        for(float v:blackWhiteMix)if(!unit(v))return "black and white mix out of range";
        for(float v:calibrationHue)if(!unit(v))return "calibration hue out of range";
        for(float v:calibrationSaturation)if(!unit(v))return "calibration saturation out of range";
        for(const auto& wheel:grading){
            if(!std::isfinite(wheel.hue)||wheel.hue<0||wheel.hue>360)return "grading hue out of range";
            if(!std::isfinite(wheel.saturation)||wheel.saturation<0||wheel.saturation>100)return "grading saturation out of range";
            if(!unit(wheel.luminance))return "grading luminance out of range";
        }
        if(!std::isfinite(gradingBlending)||gradingBlending<0||gradingBlending>100)return "grading blending out of range";
        for(const auto& curve:curves){
            if(curve.count<2||curve.count>kColorCurvePoints)return "colour curve point count out of range";
            for(int i=0;i<curve.count;++i){
                const auto& p=curve.points[i];
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.x>1||p.y<0||p.y>1)return "colour curve point out of range";
                if(i>0&&p.x<=curve.points[i-1].x)return "colour curve points must be sorted by x";
            }
        }
        if(!std::isfinite(lutStrength)||lutStrength<0||lutStrength>100)return "lut strength out of range";
        if(lutInputSpace<kLutInputCineon||lutInputSpace>kLutInputPq)return "lut input space out of range";
        if(lutName.back()!=0)return "lut name missing terminator";
        for(wchar_t c:lutName){if(c==0)break;if(c<32||c==L'\\'||c==L'/'||c==L':')return "lut name contains invalid characters";}
        const auto within=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
        if(!within(devignetteStrength,0,0.95f))return "devignette strength out of range";
        if(!within(devignetteFalloff,0.5f,6))return "devignette falloff out of range";
        if(!within(devignetteCenterY,-25,25))return "devignette vertical centre out of range";
        if(!within(devignetteProtect,0,1))return "devignette highlight protection out of range";
        if(devignetteShape!=kDevignetteCircle&&devignetteShape!=kDevignetteEllipse)return "devignette shape out of range";
        if(!within(devignetteMapThreshold,0.3f,0.9f))return "devignette detection threshold out of range";
        for(float v:{devignetteMaskLeft,devignetteMaskTop,devignetteMaskRight,devignetteMaskBottom})
            if(!within(v,0,100))return "devignette fixed region out of range";
        return {};
    }
};

// PresetStore schema v19: one appended block per entry. The field order is part
// of the on-disk format - never reorder without a new version. The stream is
// narrow UTF-8 (PresetStore owns the wide/UTF-8 conversion of lutPath) so this
// header stays free of platform includes.
// Devignette block (appended to a colour block only by the format versions that
// carry it: PresetStore v40, preset library v27, chain session v22, colour look
// v3). Field order is part of those formats.
inline void writeDevignette(std::ostream& out,const ColorSettings& c){
    out<<c.devignetteStrength<<' '<<c.devignetteFalloff<<' '<<c.devignetteCenterY<<' '<<c.devignetteProtect<<' '<<c.devignetteShape<<' '
       <<(c.devignetteHudAuto?1:0)<<' '<<c.devignetteMapThreshold<<' '<<(c.devignetteMaskOn?1:0)<<' '
       <<c.devignetteMaskLeft<<' '<<c.devignetteMaskTop<<' '<<c.devignetteMaskRight<<' '<<c.devignetteMaskBottom;
}
inline bool readDevignette(std::istream& in,ColorSettings& c){
    int hud=0,mask=0;
    if(!(in>>c.devignetteStrength>>c.devignetteFalloff>>c.devignetteCenterY>>c.devignetteProtect>>c.devignetteShape>>hud
          >>c.devignetteMapThreshold>>mask>>c.devignetteMaskLeft>>c.devignetteMaskTop>>c.devignetteMaskRight
          >>c.devignetteMaskBottom)||(hud!=0&&hud!=1)||(mask!=0&&mask!=1))return false;
    c.devignetteHudAuto=hud!=0;
    c.devignetteMaskOn=mask!=0;
    return true;
}
// `devignette` appends the devignette block; only callers writing a format
// version that carries it may pass true.
inline void writeColorSettings(std::ostream& out,const ColorSettings& c,const std::string& lutPathUtf8,bool devignette=false){
    out<<(c.enabled?1:0)<<' '<<c.temperature<<' '<<c.tint<<' '<<c.exposure<<' '<<c.contrast<<' '<<c.highlights<<' '<<c.shadows<<' '<<c.whites<<' '<<c.blacks<<' '
       <<c.texture<<' '<<c.clarity<<' '<<c.dehaze<<' '<<c.vibrance<<' '<<c.saturation<<' '
       <<c.paramHighlights<<' '<<c.paramLights<<' '<<c.paramDarks<<' '<<c.paramShadows<<' '
       <<c.splitHighlights<<' '<<c.splitMidtones<<' '<<c.splitShadows<<' '
       <<(c.blackWhite?1:0)<<' '<<c.gradingBlending<<' '<<c.gradingBalance<<' '<<c.calibrationShadowTint<<' '
       <<c.lutStrength<<' '<<c.lutInputSpace<<' '<<std::quoted(lutPathUtf8);
    // v19 keeps the per-section bypass after the LUT reference, so v18 files stay
    // readable (they simply have no mask) and the curves keep their positions.
    out<<' '<<c.groupBypassMask;
    for(const auto& curve:c.curves){
        out<<' '<<curve.count;
        for(int i=0;i<curve.count;++i)out<<' '<<curve.points[std::size_t(i)].x<<' '<<curve.points[std::size_t(i)].y;
    }
    for(float v:c.mixerHue)out<<' '<<v;
    for(float v:c.mixerSaturation)out<<' '<<v;
    for(float v:c.mixerLuminance)out<<' '<<v;
    for(float v:c.blackWhiteMix)out<<' '<<v;
    for(const auto& wheel:c.grading)out<<' '<<wheel.hue<<' '<<wheel.saturation<<' '<<wheel.luminance;
    for(float v:c.calibrationHue)out<<' '<<v;
    for(float v:c.calibrationSaturation)out<<' '<<v;
    if(devignette){out<<' ';writeDevignette(out,c);}
}
// `schemaVersion` is the *file* version: v18 files stop after the LUT reference,
// v19 adds the per-section bypass mask. Callers pass their file's version so old
// presets keep loading. `devignette` reads the devignette block, which the
// caller's own format version decides (it is not implied by schemaVersion).
inline bool readColorSettings(std::istream& in,ColorSettings& c,std::string& lutPathUtf8,int schemaVersion=19,bool devignette=false){
    int blackWhite=0,enabled=0;
    if(!(in>>enabled>>c.temperature>>c.tint>>c.exposure>>c.contrast>>c.highlights>>c.shadows>>c.whites>>c.blacks
        >>c.texture>>c.clarity>>c.dehaze>>c.vibrance>>c.saturation
        >>c.paramHighlights>>c.paramLights>>c.paramDarks>>c.paramShadows
        >>c.splitHighlights>>c.splitMidtones>>c.splitShadows
        >>blackWhite>>c.gradingBlending>>c.gradingBalance>>c.calibrationShadowTint
        >>c.lutStrength>>c.lutInputSpace>>std::quoted(lutPathUtf8))||(blackWhite!=0&&blackWhite!=1)||(enabled!=0&&enabled!=1))return false;
    c.enabled=enabled!=0;
    c.blackWhite=blackWhite!=0;
    if(schemaVersion>=19&&!(in>>c.groupBypassMask))return false;
    for(auto& curve:c.curves){
        if(!(in>>curve.count))return false;
        for(int i=0;i<curve.count&&i<kColorCurvePoints;++i)if(!(in>>curve.points[std::size_t(i)].x>>curve.points[std::size_t(i)].y))return false;
    }
    for(float& v:c.mixerHue)if(!(in>>v))return false;
    for(float& v:c.mixerSaturation)if(!(in>>v))return false;
    for(float& v:c.mixerLuminance)if(!(in>>v))return false;
    for(float& v:c.blackWhiteMix)if(!(in>>v))return false;
    for(auto& wheel:c.grading)if(!(in>>wheel.hue>>wheel.saturation>>wheel.luminance))return false;
    for(float& v:c.calibrationHue)if(!(in>>v))return false;
    for(float& v:c.calibrationSaturation)if(!(in>>v))return false;
    if(devignette&&!readDevignette(in,c))return false;
    return c.validate().empty();
}
} // namespace veyra::engine
