#include "PresetManager.h"
#include <BinaryData.h>
#include "audioplugins/common/analysis/PresetIO.h"

namespace {
namespace CommonPresetIO = audioplugins::common::analysis::PresetIO;

PresetData toJucePresetData (const audioplugins::common::analysis::PresetData& p)
{
    PresetData out;
    out.name            = juce::String (p.name);
    out.description     = juce::String (p.description);
    out.bandRmsDb        = p.bandRmsDb;
    out.bandMinCorr      = p.bandMinCorr;
    out.bandTransientDb  = p.bandTransientDb;
    out.overallRmsDb     = p.overallRmsDb;
    out.overallMinCorr   = p.overallMinCorr;
    return out;
}
} // namespace

void PresetManager::loadBuiltIn()
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        int         dataSize     = 0;
        const char* data         = BinaryData::getNamedResource (resourceName, dataSize);

        if (data == nullptr || dataSize <= 0)
            continue;

        std::string err;
        const std::string xmlText (data, static_cast<size_t> (dataSize));
        if (auto preset = CommonPresetIO::loadFromBuffer (xmlText, &err))
            presets_.push_back (toJucePresetData (*preset));
        else
            DBG ("PresetManager: failed to parse built-in resource: " << resourceName << " (" << err << ")");
    }
}

void PresetManager::mergeFromDirectory (juce::File dir)
{
    if (! dir.isDirectory())
        return;

    for (const auto& commonPreset : CommonPresetIO::loadFromDirectory (dir.getFullPathName().toStdString()))
    {
        auto preset = toJucePresetData (commonPreset);
        auto it = std::find_if (presets_.begin(), presets_.end(),
                                [&] (const PresetData& p) { return p.name == preset.name; });
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
               [] (const PresetData& a, const PresetData& b) { return a.name < b.name; });
}

void PresetManager::refresh()
{
    presets_.clear();
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());
    std::sort (presets_.begin(), presets_.end(),
               [] (const PresetData& a, const PresetData& b) { return a.name < b.name; });
}

juce::File PresetManager::getUserPresetsDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MixAdvice")
                   .getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

int PresetManager::findByName (const juce::String& name) const
{
    for (int i = 0; i < static_cast<int> (presets_.size()); ++i)
        if (presets_[static_cast<size_t> (i)].name == name)
            return i;
    return -1;
}
