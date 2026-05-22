#pragma once
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>
#include "PresetData.h"

// Loads presets from embedded BinaryData (built-in) and from a user directory.
// Discovery order:
//   1. Built-in presets from BinaryData (always present, embedded at compile time)
//   2. User presets from getUserPresetsDir() — loaded on top, override by name
// The final list is sorted alphabetically by name.
class PresetManager
{
public:
    PresetManager();

    int               getNumPresets()  const noexcept { return static_cast<int> (presets_.size()); }
    const PresetData& getPreset (int i) const noexcept { return presets_[static_cast<size_t> (i)]; }

    // Returns the directory where user presets are stored (created on first access if absent).
    juce::File getUserPresetsDir() const;

    // Re-scans the user directory and merges with the built-in presets.
    void refresh();

    // Returns the index of the preset with the given name, or -1 if not found.
    int findByName (const juce::String& name) const;

private:
    void loadBuiltIn();
    void mergeFromDirectory (juce::File dir);
    static std::optional<PresetData> parseXml (const juce::String& xmlText);
    static bool readBandArray (const juce::XmlElement& el,
                               std::array<float, BandConfig::numBands>& out);

    std::vector<PresetData> presets_;
};
