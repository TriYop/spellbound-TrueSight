#include "PresetManager.h"
#include <BinaryData.h>

// Band attribute names in XML — must match the order used in BandConfig.
static constexpr const char* kBandAttrs[] = {
    "sub", "lows", "lomid", "mids", "himid", "highs", "air"
};
static_assert (std::size (kBandAttrs) == BandConfig::numBands);

// ── XML parsing ───────────────────────────────────────────────────────────────

bool PresetManager::readBandArray (const juce::XmlElement& el,
                                   std::array<float, BandConfig::numBands>& out)
{
    for (int i = 0; i < BandConfig::numBands; ++i)
    {
        if (! el.hasAttribute (kBandAttrs[i]))
            return false;
        out[static_cast<size_t> (i)] = (float) el.getDoubleAttribute (kBandAttrs[i]);
    }
    return true;
}

std::optional<PresetData> PresetManager::parseXml (const juce::String& xmlText)
{
    auto doc = juce::XmlDocument::parse (xmlText);
    if (doc == nullptr || doc->getTagName() != "MixAdvicePreset")
        return std::nullopt;

    PresetData p;
    p.name        = doc->getStringAttribute ("name");
    p.description = doc->getStringAttribute ("description");

    if (p.name.isEmpty())
        return std::nullopt;

    auto* rmsEl       = doc->getChildByName ("BandRmsDb");
    auto* corrEl      = doc->getChildByName ("BandMinCorr");
    auto* transientEl = doc->getChildByName ("BandTransientDb");
    auto* overallEl   = doc->getChildByName ("Overall");

    if (rmsEl == nullptr || corrEl == nullptr || transientEl == nullptr || overallEl == nullptr)
        return std::nullopt;

    if (! readBandArray (*rmsEl,       p.bandRmsDb))       return std::nullopt;
    if (! readBandArray (*corrEl,      p.bandMinCorr))     return std::nullopt;
    if (! readBandArray (*transientEl, p.bandTransientDb)) return std::nullopt;

    if (! overallEl->hasAttribute ("rmsDb") || ! overallEl->hasAttribute ("minCorr"))
        return std::nullopt;

    p.overallRmsDb   = (float) overallEl->getDoubleAttribute ("rmsDb");
    p.overallMinCorr = (float) overallEl->getDoubleAttribute ("minCorr");

    return p;
}

// ── Loading ───────────────────────────────────────────────────────────────────

void PresetManager::loadBuiltIn()
{
    // BinaryData exposes every embedded file as: <name>_xml / <name>_xmlSize
    // We iterate the BinaryData resource list by name.
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        int         dataSize     = 0;
        const char* data         = BinaryData::getNamedResource (resourceName, dataSize);

        if (data == nullptr || dataSize <= 0)
            continue;

        const juce::String xmlText (juce::CharPointer_UTF8 (data), (size_t) dataSize);
        if (auto preset = parseXml (xmlText))
            presets_.push_back (std::move (*preset));
        else
            DBG ("PresetManager: failed to parse built-in resource: " << resourceName);
    }
}

void PresetManager::mergeFromDirectory (juce::File dir)
{
    if (! dir.isDirectory())
        return;

    for (const auto& file : juce::RangedDirectoryIterator (dir, false, "*.xml"))
    {
        const juce::String xmlText = file.getFile().loadFileAsString();
        auto preset = parseXml (xmlText);
        if (! preset)
        {
            DBG ("PresetManager: skipping invalid preset file: " << file.getFile().getFullPathName());
            continue;
        }

        // Override by name: replace existing entry if name matches, otherwise append.
        auto it = std::find_if (presets_.begin(), presets_.end(),
                                [&] (const PresetData& p) { return p.name == preset->name; });
        if (it != presets_.end())
            *it = std::move (*preset);
        else
            presets_.push_back (std::move (*preset));
    }
}

// ── Public interface ──────────────────────────────────────────────────────────

PresetManager::PresetManager()
{
    loadBuiltIn();
    mergeFromDirectory (getUserPresetsDir());

    std::sort (presets_.begin(), presets_.end(),
               [] (const PresetData& a, const PresetData& b) { return a.name < b.name; });
}

void PresetManager::refresh()
{
    // Keep built-in presets; reload user dir on top.
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
