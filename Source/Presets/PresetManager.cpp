#include "PresetManager.h"
#include "EmbeddedPresets.h"   // generated, see CMakeLists.txt
#include "audioplugins/common/analysis/PresetIO.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace {
namespace CommonPresetIO = audioplugins::common::analysis::PresetIO;
}

void PresetManager::loadBuiltIn()
{
    for (const auto& embedded : getEmbeddedPresets())
    {
        std::string err;
        if (auto preset = CommonPresetIO::loadFromBuffer (embedded.xmlText, &err))
            presets_.push_back (*preset);
    }
}

void PresetManager::mergeFromDirectory (const std::string& dir)
{
    if (! std::filesystem::is_directory (dir))
        return;

    for (auto& preset : CommonPresetIO::loadFromDirectory (dir))
    {
        auto it = std::find_if (presets_.begin(), presets_.end(),
                                 [&] (const auto& p) { return p.name == preset.name; });
        if (it != presets_.end())
            *it = std::move (preset);
        else
            presets_.push_back (std::move (preset));
    }
}

PresetManager::PresetManager()
{
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const auto& a, const auto& b) { return a.name < b.name; });
}

void PresetManager::refresh()
{
    presets_.clear();
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const auto& a, const auto& b) { return a.name < b.name; });
}

std::string PresetManager::getUserPresetsDir() const
{
    const char* home = std::getenv ("HOME");
    std::filesystem::path dir = (home ? std::filesystem::path (home) : std::filesystem::current_path())
                                    / ".config" / "MixAdvice" / "Presets";
    std::error_code ec;
    std::filesystem::create_directories (dir, ec);
    return dir.string();
}

int PresetManager::findByName (const std::string& name) const
{
    for (int i = 0; i < static_cast<int> (presets_.size()); ++i)
        if (presets_[static_cast<size_t> (i)].name == name)
            return i;
    return -1;
}
