// 去暗角 (devignette, Dévignetteur port): the colour settings fields, their
// validation and the colour-block serialisation, plus the settings presets.
// Older readers reject the devignette block, so a store only writes it when an
// entry uses it: PresetStore v40 (library v27 / session v22 / colour look v3 are
// covered by PresetLibraryTests and ColorLookTests). Pure CPU.
#include "veyra/engine/ColorSettings.h"
#include "veyra/engine/PresetStore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace veyra::engine;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::cout<<"FAIL "<<why<<'\n';}}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
ColorSettings corrected(){
    ColorSettings c;
    c.enabled=true;
    c.applyGta6Devignette();
    c.devignetteShape=ColorSettings::kDevignetteRectangle;c.devignetteRoundness=35;
    c.devignetteCenterX=-3.5f;c.devignetteStretch=12.5f;c.devignetteStart=10;
    c.devignetteCenterY=6.5f;c.devignetteProtect=0.35f;c.devignetteHudAuto=false;c.devignetteMapThreshold=0.72f;
    c.devignetteMaskOn=true;c.devignetteMaskLeft=1.5f;c.devignetteMaskTop=70;c.devignetteMaskRight=20;c.devignetteMaskBottom=98.5f;
    return c;
}
bool roundTrip(const ColorSettings& in,bool block,ColorSettings& out){
    std::ostringstream o;o.imbue(std::locale::classic());o.precision(9);
    writeColorSettings(o,in,"",block);
    std::istringstream i(o.str());i.imbue(std::locale::classic());
    std::string lut;
    if(!readColorSettings(i,out,lut,19,block))return false;
    i>>std::ws;
    out.enabled=in.enabled;
    return i.eof();
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||!std::filesystem::path(argv[1]).is_absolute())return 2;
    const std::filesystem::path root=argv[1];if(!std::filesystem::create_directories(root))return 2;

    // Defaults are neutral: strength 0 is off and nothing is written.
    const ColorSettings defaults;
    check(defaults.neutral()&&!defaults.hasDevignette()&&defaults.validate().empty(),"default devignette fields are neutral and valid");
    ColorSettings gta;gta.applyGta6Devignette();
    check(gta.devignetteStrength==0.24f&&gta.devignetteFalloff==1.45f&&gta.devignetteShape==ColorSettings::kDevignetteEllipse,
          "the GTA 6 preset is the measured vignette (0.24, 1.45, elliptical)");
    check(!gta.neutral()&&gta.hasDevignette(),"a devignette value makes the grade non-neutral");
    auto tuned=corrected();tuned.applyGta6Devignette();
    check(tuned.devignetteCenterX==0&&tuned.devignetteCenterY==0&&tuned.devignetteStretch==0&&tuned.devignetteStart==0&&
          tuned.devignetteShape==ColorSettings::kDevignetteEllipse,"the GTA 6 preset restores a centred, unstretched ellipse");
    auto reset=corrected();reset.resetDevignette();
    check(!reset.hasDevignette(),"resetting the group restores every devignette default");

    // Validation follows the Dévignetteur ranges.
    const auto rejects=[](auto edit){auto c=ColorSettings{};edit(c);return !c.validate().empty();};
    check(rejects([](ColorSettings& c){c.devignetteStrength=0.96f;}),"strength above 0.95 is refused");
    check(rejects([](ColorSettings& c){c.devignetteFalloff=0.4f;}),"falloff below 0.5 is refused");
    check(rejects([](ColorSettings& c){c.devignetteCenterY=-51;}),"a vertical centre beyond 50 % is refused");
    check(rejects([](ColorSettings& c){c.devignetteCenterX=51;}),"a horizontal centre beyond 50 % is refused");
    check(rejects([](ColorSettings& c){c.devignetteStretch=-51;}),"a stretch beyond 50 % is refused");
    check(rejects([](ColorSettings& c){c.devignetteStart=81;}),"a gradient start above 80 % is refused");
    check(rejects([](ColorSettings& c){c.devignetteRoundness=101;}),"a roundness above 100 % is refused");
    check(!rejects([](ColorSettings& c){c.devignetteShape=ColorSettings::kDevignetteRectangle;}),"the rectangle shape is accepted");
    check(rejects([](ColorSettings& c){c.devignetteProtect=1.1f;}),"protection above 1 is refused");
    check(rejects([](ColorSettings& c){c.devignetteShape=3;}),"an unknown shape is refused");
    check(rejects([](ColorSettings& c){c.devignetteMapThreshold=0.95f;}),"a threshold above 0.9 is refused");
    check(rejects([](ColorSettings& c){c.devignetteMaskBottom=101;}),"a fixed region outside 0..100 % is refused");
    check(rejects([](ColorSettings& c){c.devignetteStrength=std::numeric_limits<float>::quiet_NaN();}),"a NaN strength is refused");

    // Colour block: the devignette block only when asked for; the old layout is unchanged.
    {
        std::ostringstream plain,legacy;
        writeColorSettings(plain,corrected(),"",false);
        writeColorSettings(legacy,corrected(),"");
        check(plain.str()==legacy.str(),"without the flag the colour block keeps its v19 layout");
        ColorSettings back;
        check(roundTrip(corrected(),true,back)&&back==corrected(),"every devignette field round trips");
        ColorSettings old;
        check(roundTrip(corrected(),false,old)&&!old.hasDevignette(),"a reader without the block keeps the defaults");
        std::istringstream broken("0.24 1.45 0 0 0 0 50 0.7 1 2 0.6 0 0.5 77 18 99");ColorSettings c;
        check(!readDevignette(broken,c),"a boolean outside 0/1 is rejected");
    }

    // Settings presets: v40 only when an entry uses devignette.
    {
        EnhancementSettings plain;
        PresetStore store(root/L"plain.txt");
        check(store.load()&&store.put(L"plain",plain)&&!bytes(root/L"plain.txt").starts_with("VEYRA_PRESETS 40"),
              "settings without devignette keep the older version");
        EnhancementSettings s;s.color=corrected();
        s.additionalColorCount=1;s.additionalColors[0].enabled=true;s.additionalColors[0].devignetteStrength=0.1f;
        PresetStore writer(root/L"devignette.txt");
        check(writer.load()&&writer.put(L"gta",s)&&bytes(root/L"devignette.txt").starts_with("VEYRA_PRESETS 40\n"),
              "a devignette value writes v40");
        PresetStore reader(root/L"devignette.txt");
        check(reader.load()&&reader.entries().size()==1&&reader.entries()[0].settings.color==s.color&&
              reader.entries()[0].settings.additionalColors[0]==s.additionalColors[0],
              "v40 restores the primary and additional colour devignette fields");
    }

    std::cout<<(failures?"FAIL":"PASS")<<": devignette settings "<<checks-failures<<'/'<<checks<<'\n';
    return failures?1:0;
}
