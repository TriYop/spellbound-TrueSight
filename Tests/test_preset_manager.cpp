#include "Presets/PresetManager.h"
#include "test_runner.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

int main()
{
    // PresetManager reads its user dir from $HOME (see getUserPresetsDir()) --
    // point HOME at a throwaway temp dir so this test never touches the
    // developer's real ~/.config/MixAdvice/Presets.
    const auto tmpHome = std::filesystem::temp_directory_path() / "truesight_preset_manager_test";
    std::filesystem::remove_all (tmpHome);
    std::filesystem::create_directories (tmpHome / ".config" / "MixAdvice" / "Presets");
    // setenv() is POSIX-only; MSVC's C runtime doesn't provide it (caught by
    // windows-latest CI: error C3861 'setenv': identifier not found) -- use
    // its portable _putenv_s() equivalent there instead. This only needs to
    // satisfy PresetManager::getUserPresetsDir()'s own std::getenv("HOME")
    // read below, not model real Windows user-profile conventions.
#ifdef _WIN32
    _putenv_s ("HOME", tmpHome.string().c_str());
#else
    setenv ("HOME", tmpHome.string().c_str(), 1);
#endif

    {
        // Schema confirmed against Common's PresetIO.cpp (parsePresetNode) and
        // a real file under Presets/*.xml: root <MixAdvicePreset name description>,
        // then <BandRmsDb>/<BandMinCorr>/<BandTransientDb> each with all seven
        // band attributes (sub/lows/lomid/mids/himid/highs/air), then <Overall
        // rmsDb minCorr>. All of these are required or the file fails to parse.
        std::ofstream f (tmpHome / ".config" / "MixAdvice" / "Presets" / "ZZZ_UserTest.xml");
        f << R"(<?xml version="1.0"?><MixAdvicePreset name="ZZZ_UserTest" description="test">)"
             R"(<BandRmsDb sub="-20" lows="-20" lomid="-20" mids="-20" himid="-20" highs="-20" air="-20"/>)"
             R"(<BandMinCorr sub="0.5" lows="0.5" lomid="0.5" mids="0.5" himid="0.5" highs="0.5" air="0.5"/>)"
             R"(<BandTransientDb sub="6" lows="6" lomid="6" mids="6" himid="6" highs="6" air="6"/>)"
             R"(<Overall rmsDb="-18" minCorr="0.6"/></MixAdvicePreset>)";
    }

    PresetManager mgr;
    CHECK_MSG (mgr.getNumPresets() >= 1, "expected at least the compiled-in factory presets");

    const int idx = mgr.findByName ("ZZZ_UserTest");
    CHECK_MSG (idx >= 0, "expected the user-directory preset to be discovered and merged");
    CHECK_MSG (mgr.getPreset (idx).name == "ZZZ_UserTest", "preset name round-trip mismatch");

    std::filesystem::remove_all (tmpHome);
    TEST_SUMMARY();
}
