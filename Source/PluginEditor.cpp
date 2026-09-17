#include "PluginEditor.h"
#include "Analysis/BandConfig.h"
#include "Analysis/AdviceAdapter.h"
#include "Presets/PresetManager.h"
#include "audioplugins/common/analysis/AdviceSet.h"
#include "audioplugins/common/analysis/Report.h"
#include <algorithm>
#include <cmath>

// ── Colours ───────────────────────────────────────────────────────────────────
static const juce::Colour kBackground  { 0xff1a1a2e };
static const juce::Colour kHeaderBg    { 0xff22223a };
static const juce::Colour kGridLine    { 0xff2a2a4e };
static const juce::Colour kBarL        { 0xff4488cc };
static const juce::Colour kBarR        { 0xff44aacc };
static const juce::Colour kLabelText   { 0xffaaaacc };
static const juce::Colour kDimText     { 0xff6666aa };
static const juce::Colour kRefLine     { 0xffffaa00 };  // amber reference / target

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int kHeaderH   = 36;
static constexpr int kScaleW    = 52;    // left dB-scale column
static constexpr int kCorrH      = 18;   // correlation strip height
static constexpr int kTransientH = 16;   // transient energy strip height
static constexpr int kLabelH     = 20;   // band-name label row
static constexpr int kBarGap    = 4;     // gap between L and R bars within a slot
static constexpr int kOverallW  = 100;   // overall energy meter panel width
static constexpr int kPanelGap  = 8;     // gap between band area and overall panel
static constexpr int kAdviceH   = 156;   // mastering advice panel at bottom (EQ/comp + mixbus/loudness)
static constexpr int kResonanceH = 46;   // resonance-cuts strip, above the advice panel

// Percentile stats need a few seconds of audio before they're meaningful — until
// then, advice reference levels fall back to the existing avg/peak blend.
static constexpr float kPercentileWarmupSec = 2.0f;

// ── Advice adapter glue (JUCE-touching; kept out of the framework-free
// Source/Analysis/AdviceAdapter.h/.cpp so Tests/ can include that header
// without pulling JUCE in) ────────────────────────────────────────────────────
namespace {
audioplugins::common::analysis::PresetData toCommonPresetData (const ::PresetData& preset)
{
    audioplugins::common::analysis::PresetData out;
    out.name            = preset.name.toStdString();
    out.description     = preset.description.toStdString();
    out.bandRmsDb        = preset.bandRmsDb;
    out.bandMinCorr      = preset.bandMinCorr;
    out.bandTransientDb  = preset.bandTransientDb;
    out.overallRmsDb     = preset.overallRmsDb;
    out.overallMinCorr   = preset.overallMinCorr;
    return out;
}
} // namespace

// ── Helpers ───────────────────────────────────────────────────────────────────
static float dbToNorm (float db) noexcept
{
    constexpr float rng = BandConfig::displayCeilDb - BandConfig::displayFloorDb;
    return std::clamp ((db - BandConfig::displayFloorDb) / rng, 0.f, 1.f);
}

// ── Constructor / destructor ──────────────────────────────────────────────────
MixAdviceAudioProcessorEditor::MixAdviceAudioProcessorEditor (MixAdviceAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // Populate ComboBox with preset names (JUCE IDs are 1-based)
    const auto& mgr = processorRef.getPresetManager();
    for (int i = 0; i < mgr.getNumPresets(); ++i)
        presetSelector_.addItem (mgr.getPreset (i).name, i + 1);

    presetSelector_.setSelectedId (processorRef.getCurrentProgram() + 1,
                                   juce::dontSendNotification);

    presetSelector_.onChange = [this]
    {
        processorRef.setCurrentProgram (presetSelector_.getSelectedId() - 1);
    };

    // Style
    presetSelector_.setColour (juce::ComboBox::backgroundColourId,  juce::Colour (0xff2a2a4e));
    presetSelector_.setColour (juce::ComboBox::textColourId,         kLabelText);
    presetSelector_.setColour (juce::ComboBox::outlineColourId,      juce::Colour (0xff44446a));
    presetSelector_.setColour (juce::ComboBox::arrowColourId,        kDimText);
    presetSelector_.setColour (juce::ComboBox::focusedOutlineColourId, kRefLine);

    addAndMakeVisible (presetSelector_);

    exportButton_.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2a2a4e));
    exportButton_.setColour (juce::TextButton::textColourOffId,  kLabelText);
    exportButton_.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    exportButton_.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff44446a));
    exportButton_.onClick = [this] { exportAdvice(); };
    exportButton_.setEnabled (false);
    addAndMakeVisible (exportButton_);

    setSize (900, 660);
    startTimerHz (30);
}

MixAdviceAudioProcessorEditor::~MixAdviceAudioProcessorEditor()
{
    stopTimer();
}

// ── Timer ─────────────────────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::timerCallback()
{
    // Sync ComboBox if the processor's preset changed externally (e.g. DAW automation)
    const int processorPreset = processorRef.getCurrentProgram();
    if (presetSelector_.getSelectedId() - 1 != processorPreset)
        presetSelector_.setSelectedId (processorPreset + 1, juce::dontSendNotification);

    // Export button: enabled when stopped and advice data is available
    const auto snap       = processorRef.getAnalysisResult().read();
    const bool hasAdvice  = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f > -99.f;
    const bool isPlaying  = processorRef.isCurrentlyPlaying();
    exportButton_.setEnabled (hasAdvice && !isPlaying);

    repaint();
}

// ── Resized ───────────────────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::resized()
{
    const int cbW = 220;
    const int cbH = 22;
    const int cbY = (kHeaderH - cbH) / 2;
    presetSelector_.setBounds (getWidth() / 2 - cbW / 2 - 60, cbY, cbW, cbH);

    // Export button: bottom of the scale column inside the advice panel
    const int btnH    = 18;
    const int adviceY = getHeight() - kAdviceH;
    exportButton_.setBounds (2, adviceY + kAdviceH - btnH - 4, kScaleW - 4, btnH);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (kBackground);

    // ── Header ────────────────────────────────────────────────────────────────
    g.setColour (kHeaderBg);
    g.fillRect  (bounds.withHeight (kHeaderH));

    g.setColour (juce::Colours::white);
    g.setFont   (juce::FontOptions (15.0f).withStyle ("Bold"));
    g.drawText  ("MixAdvice", bounds.withHeight (kHeaderH).withRight (presetSelector_.getX() - 8),
                 juce::Justification::centredRight);

    // Preset description — dim text to the right of the ComboBox
    {
        const int descX = presetSelector_.getRight() + 10;
        const int descRight = bounds.getRight() - 110;   // leave room for the L/R/Ref legend
        if (descRight > descX)
        {
            g.setFont   (juce::FontOptions (10.5f));
            g.setColour (kDimText);
            g.drawText  (processorRef.getPresetManager().getPreset (processorRef.getCurrentProgram()).description,
                         descX, 0, descRight - descX, kHeaderH,
                         juce::Justification::centredLeft, true);
        }
    }

    // L / R colour legend in the header (top-right)
    {
        const int swatchSz  = 10;
        const int legendY   = (kHeaderH - swatchSz) / 2;
        const int rightEdge = bounds.getRight() - 12;

        g.setColour (kBarR);
        g.fillRect  (rightEdge - swatchSz, legendY, swatchSz, swatchSz);
        g.setColour (kLabelText);
        g.setFont   (juce::FontOptions (11.0f));
        g.drawText  ("R", rightEdge - swatchSz - 14, legendY - 1, 12, swatchSz + 2,
                     juce::Justification::centredRight);

        g.setColour (kBarL);
        g.fillRect  (rightEdge - swatchSz - 34, legendY, swatchSz, swatchSz);
        g.setColour (kLabelText);
        g.drawText  ("L", rightEdge - swatchSz - 34 - 14, legendY - 1, 12, swatchSz + 2,
                     juce::Justification::centredRight);

        // Amber reference swatch
        const int refX = rightEdge - swatchSz - 68;
        g.setColour (kRefLine);
        g.fillRect  (refX, legendY, swatchSz, swatchSz);
        g.setColour (kLabelText);
        g.drawText  ("Ref", refX - 26, legendY - 1, 24, swatchSz + 2,
                     juce::Justification::centredRight);
    }

    // ── Layout zones ──────────────────────────────────────────────────────────
    const auto adviceArea    = bounds.withTop (bounds.getBottom() - kAdviceH);
    const auto resonanceArea = bounds.withTop (adviceArea.getY() - kResonanceH).withHeight (kResonanceH);
    const auto workArea      = bounds.withTrimmedTop (kHeaderH).withBottom (resonanceArea.getY());
    const auto overallPanel = workArea.withLeft (workArea.getRight() - kOverallW);
    const auto leftOfPanel  = workArea.withRight (overallPanel.getX() - kPanelGap);
    const auto scaleArea    = leftOfPanel.withWidth (kScaleW);
    const auto plotArea     = leftOfPanel.withTrimmedLeft (kScaleW);
    const auto labelRow      = plotArea.withTop (plotArea.getBottom() - kLabelH);
    const auto corrRow       = plotArea.withTop (labelRow.getY() - kCorrH).withHeight (kCorrH);
    const auto transientRow  = plotArea.withTop (corrRow.getY() - kTransientH).withHeight (kTransientH);
    const auto barArea       = plotArea.withBottom (transientRow.getY()).reduced (0, 4);
    const auto dbArea        = scaleArea.withBottom (transientRow.getY());

    const auto snap    = processorRef.getAnalysisResult().read();
    const auto& preset = processorRef.getPresetManager().getPreset (processorRef.getCurrentProgram());

    drawDbScale         (g, dbArea);
    drawBandBars        (g, barArea, snap, preset);
    drawTransientStrip  (g, transientRow, snap, preset);
    drawCorrStrip       (g, corrRow, snap, preset);
    drawOverallMeter    (g, overallPanel, snap, preset);
    drawResonancePanel  (g, resonanceArea, snap);
    drawAdvicePanel     (g, adviceArea, snap, preset);

    // ── Band name labels ──────────────────────────────────────────────────────
    g.setFont   (juce::FontOptions (11.0f));
    g.setColour (kLabelText);
    const float slotW = static_cast<float> (plotArea.getWidth()) / BandConfig::numBands;
    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    {
        const auto x = plotArea.getX() + static_cast<int> (static_cast<float> (i) * slotW);
        g.drawText (BandConfig::bandNames[i],
                    juce::Rectangle<int> (x, labelRow.getY(), (int) slotW, kLabelH),
                    juce::Justification::centred);
    }

    // ── Row labels in the scale column ───────────────────────────────────────
    g.setFont   (juce::FontOptions (9.0f));
    g.setColour (kDimText);
    g.drawText  ("Transient",
                 scaleArea.getX(), transientRow.getY(),
                 scaleArea.getWidth(), transientRow.getHeight(),
                 juce::Justification::centredRight);
    g.drawText  ("Mono compat.",
                 scaleArea.getX(), corrRow.getY(),
                 scaleArea.getWidth(), corrRow.getHeight(),
                 juce::Justification::centredRight);
}

// ── dB scale ─────────────────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::drawDbScale (juce::Graphics& g,
                                                  juce::Rectangle<int> area) const
{
    g.setFont   (juce::FontOptions (9.0f));
    g.setColour (kDimText);
    g.drawText  ("dBFS", area.getX(), area.getY(), area.getWidth(), 12,
                 juce::Justification::centredRight);

    g.setFont (juce::FontOptions (10.0f));

    const float h   = static_cast<float> (area.getHeight());
    const float top = static_cast<float> (area.getY());

    for (int db : { 0, -12, -24, -36, -48, -60 })
    {
        const float y = top + h * (1.f - dbToNorm (static_cast<float> (db)));

        g.setColour (kGridLine);
        g.drawHorizontalLine (juce::roundToInt (y),
                              static_cast<float> (area.getX()),
                              static_cast<float> (getWidth()));

        g.setColour (kLabelText);
        g.drawText  (juce::String (db),
                     area.getX(), juce::roundToInt (y) - 6, area.getWidth(), 12,
                     juce::Justification::centredRight);
    }
}

// ── Band bars (L left half, R right half; amber reference line) ───────────────
void MixAdviceAudioProcessorEditor::drawBandBars (juce::Graphics& g,
                                                   juce::Rectangle<int> area,
                                                   const AnalysisResult::Snapshot& snap,
                                                   const PresetData& preset) const
{
    const float slotW = static_cast<float> (area.getWidth()) / BandConfig::numBands;
    const float h     = static_cast<float> (area.getHeight());
    const float areaB = static_cast<float> (area.getBottom());

    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    {
        const float slotX = static_cast<float> (area.getX()) + static_cast<float> (i) * slotW;
        const float halfW = (slotW - kBarGap * 3.f) / 2.f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float db     = ch == 0 ? snap.rmsDbL[i]     : snap.rmsDbR[i];
            const float peakDb = ch == 0 ? snap.peakRmsDbL[i] : snap.peakRmsDbR[i];
            const float barX   = slotX + kBarGap + ch * (halfW + kBarGap);

            g.setColour (ch == 0 ? kBarL : kBarR);
            g.fillRect  (juce::Rectangle<float> (barX, areaB - dbToNorm (db) * h,
                                                 halfW, dbToNorm (db) * h));

            // Peak tick
            const float peakY = areaB - dbToNorm (peakDb) * h;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect  (juce::Rectangle<float> (barX, peakY - 1.f, halfW, 2.f));
        }

        // Amber reference line at the preset target dBFS for this band
        const float refY = areaB - dbToNorm (preset.bandRmsDb[i]) * h;
        g.setColour (kRefLine);
        g.drawHorizontalLine (juce::roundToInt (refY),
                              slotX + kBarGap,
                              slotX + slotW - kBarGap);
    }
}

// ── Transient energy strip (per-band crest factor, 0–30 dB) ──────────────────
void MixAdviceAudioProcessorEditor::drawTransientStrip (juce::Graphics& g,
                                                         juce::Rectangle<int> area,
                                                         const AnalysisResult::Snapshot& snap,
                                                         const PresetData& preset) const
{
    constexpr float kMaxCrestDb = 30.f;

    const float slotW = static_cast<float> (area.getWidth()) / BandConfig::numBands;
    const float areaX = static_cast<float> (area.getX());
    const float areaY = static_cast<float> (area.getY());
    const float areaH = static_cast<float> (area.getHeight());

    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    {
        const float slotX  = areaX + static_cast<float> (i) * slotW;
        const float innerX = slotX + 2.f;
        const float innerW = slotW - 4.f;

        // Average L+R crest factor; fill bar left-to-right representing 0–30 dB
        const float crestDb  = (snap.transientDbL[i] + snap.transientDbR[i]) * 0.5f;
        const float fillFrac = std::clamp (crestDb / kMaxCrestDb, 0.f, 1.f);

        // Dark trough (unfilled portion)
        g.setColour (juce::Colour (0xff1a1a38));
        g.fillRoundedRectangle (innerX, areaY, innerW, areaH, 3.f);

        // Filled bar in a neutral teal colour
        if (fillFrac > 0.f)
        {
            g.setColour (juce::Colour (0xff2299aa));
            g.fillRoundedRectangle (innerX, areaY, innerW * fillFrac, areaH, 3.f);
        }

        // Amber reference tick at the preset target crest factor
        const float refFrac = std::clamp (preset.bandTransientDb[i] / kMaxCrestDb, 0.f, 1.f);
        const float tickX   = innerX + innerW * refFrac;
        g.setColour (kRefLine);
        g.fillRect  (juce::Rectangle<float> (tickX - 1.f, areaY + 1.f, 2.f, areaH - 2.f));

        // Amber border when more than 6 dB below the preset target (under-transient)
        if (crestDb < preset.bandTransientDb[i] - 6.f)
        {
            g.setColour (kRefLine);
            g.drawRoundedRectangle (innerX, areaY, innerW, areaH, 3.f, 1.5f);
        }
    }
}

// ── Correlation colour strip ──────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::drawCorrStrip (juce::Graphics& g,
                                                    juce::Rectangle<int> area,
                                                    const AnalysisResult::Snapshot& snap,
                                                    const PresetData& preset) const
{
    const float slotW = static_cast<float> (area.getWidth()) / BandConfig::numBands;

    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    {
        const float x    = static_cast<float> (area.getX()) + static_cast<float> (i) * slotW + 2.f;
        const float corr = snap.correlation[i];

        g.setColour (corrColour (corr));
        g.fillRoundedRectangle (x, static_cast<float> (area.getY()),
                                slotW - 4.f, static_cast<float> (area.getHeight()), 3.f);

        // Amber border when below the preset's minimum acceptable correlation
        if (corr < preset.bandMinCorr[i])
        {
            g.setColour (kRefLine);
            g.drawRoundedRectangle (x, static_cast<float> (area.getY()),
                                    slotW - 4.f, static_cast<float> (area.getHeight()),
                                    3.f, 1.5f);
        }
    }
}

// ── Overall energy meter ──────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::drawOverallMeter (juce::Graphics& g,
                                                       juce::Rectangle<int> area,
                                                       const AnalysisResult::Snapshot& snap,
                                                       const PresetData& preset) const
{
    g.setColour (juce::Colour (0xff1f1f38));
    g.fillRect  (area);

    // ── Layout (build from bottom up) ─────────────────────────────────────────
    constexpr int titleH    = 18;
    constexpr int chLabelH  = 16;
    constexpr int monoHdrH  = 12;
    constexpr int monoRowH  = 15;

    const auto intRow      = area.withTop    (area.getBottom() - monoRowH)     .withHeight (monoRowH);
    const auto rtRow       = area.withTop    (intRow.getY()    - monoRowH)     .withHeight (monoRowH);
    const auto monoHdrRow  = area.withTop    (rtRow.getY()     - monoHdrH)     .withHeight (monoHdrH);
    const auto chLabelRow  = area.withTop    (monoHdrRow.getY() - chLabelH)    .withHeight (chLabelH);
    const auto barsArea    = area.withTop    (titleH).withBottom (chLabelRow.getY()).reduced (0, 2);

    // ── Title ─────────────────────────────────────────────────────────────────
    g.setFont   (juce::FontOptions (10.0f).withStyle ("Bold"));
    g.setColour (kLabelText);
    g.drawText  ("Overall Level", area.getX(), area.getY() + 3, area.getWidth(), titleH,
                 juce::Justification::centred);

    // ── L / R bars ────────────────────────────────────────────────────────────
    const float h     = static_cast<float> (barsArea.getHeight());
    const float areaB = static_cast<float> (barsArea.getBottom());
    const float barW  = (static_cast<float> (barsArea.getWidth()) - kBarGap * 3.f) / 2.f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float db     = ch == 0 ? snap.overallRmsDbL  : snap.overallRmsDbR;
        const float peakDb = ch == 0 ? snap.peakOverallDbL : snap.peakOverallDbR;
        const float barX   = static_cast<float> (barsArea.getX()) + kBarGap + ch * (barW + kBarGap);

        g.setColour (ch == 0 ? kBarL : kBarR);
        g.fillRect  (juce::Rectangle<float> (barX, areaB - dbToNorm (db) * h,
                                             barW, dbToNorm (db) * h));

        const float peakY = areaB - dbToNorm (peakDb) * h;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect  (juce::Rectangle<float> (barX, peakY - 1.f, barW, 2.f));

        g.setFont   (juce::FontOptions (10.0f));
        g.setColour (ch == 0 ? kBarL : kBarR);
        g.drawText  (ch == 0 ? "L" : "R",
                     juce::Rectangle<int> ((int) barX, chLabelRow.getY(), (int) barW, chLabelH),
                     juce::Justification::centred);
    }

    // Amber reference line at the preset overall target dBFS
    const float refY = areaB - dbToNorm (preset.overallRmsDb) * h;
    g.setColour (kRefLine);
    g.drawHorizontalLine (juce::roundToInt (refY),
                          static_cast<float> (barsArea.getX()),
                          static_cast<float> (barsArea.getRight()));

    // ── Divider + Mono compat. section ────────────────────────────────────────
    g.setColour (kGridLine);
    g.drawHorizontalLine (monoHdrRow.getY(), (float) area.getX() + 4.f, (float) area.getRight() - 4.f);

    g.setFont   (juce::FontOptions (9.0f).withStyle ("Bold"));
    g.setColour (kDimText);
    g.drawText  ("Mono compat.", monoHdrRow, juce::Justification::centred);

    auto drawMonoRow = [&] (juce::Rectangle<int> row, float corr,
                             const char* label, bool showValue, float minCorr)
    {
        constexpr int lblW = 22;
        const auto lblRect   = row.withWidth (lblW);
        const auto stripRect = row.withTrimmedLeft (lblW).reduced (2, 2);

        g.setFont   (juce::FontOptions (8.5f));
        g.setColour (kDimText);
        g.drawText  (label, lblRect, juce::Justification::centred);

        g.setColour (corrColour (corr));
        g.fillRoundedRectangle (stripRect.toFloat(), 2.f);

        // Amber border when below preset's minimum acceptable overall correlation
        if (corr < minCorr)
        {
            g.setColour (kRefLine);
            g.drawRoundedRectangle (stripRect.toFloat(), 2.f, 1.5f);
        }

        if (showValue)
        {
            g.setFont   (juce::FontOptions (8.5f));
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.drawText  (juce::String (corr, 2), stripRect, juce::Justification::centred);
        }
    };

    drawMonoRow (rtRow,  snap.overallCorrelation,    "RT",  false, preset.overallMinCorr);
    drawMonoRow (intRow, snap.integratedCorrelation,  "Int", true,  preset.overallMinCorr);
}

// ── Resonance-cut suggestions ─────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::drawResonancePanel (juce::Graphics& g,
                                                         juce::Rectangle<int> area,
                                                         const AnalysisResult::Snapshot& snap) const
{
    g.setColour (juce::Colour (0xff17172a));
    g.fillRect  (area);
    g.setColour (kGridLine);
    g.drawHorizontalLine (area.getY(), 0.f, static_cast<float> (getWidth()));

    // Mirror the meter column layout so tags line up under the band bars
    const auto overallPanel = area.withLeft (area.getRight() - kOverallW);
    const auto leftOfPanel  = area.withRight (overallPanel.getX() - kPanelGap);
    const auto scaleCol     = leftOfPanel.withWidth (kScaleW);
    const auto plotArea     = leftOfPanel.withTrimmedLeft (kScaleW);

    g.setFont   (juce::FontOptions (9.0f));
    g.setColour (kDimText);
    g.drawText  ("Resonance", scaleCol.getX(), area.getY(), scaleCol.getWidth(), area.getHeight(),
                 juce::Justification::centredRight);

    const int count = std::clamp (snap.resonanceCount, 0, AnalysisResult::maxResonances);

    if (count == 0)
    {
        g.setFont   (juce::FontOptions (10.f));
        g.setColour (kDimText);
        g.drawText  ("No resonances detected", plotArea, juce::Justification::centred);
        return;
    }

    const float slotW = static_cast<float> (plotArea.getWidth()) / AnalysisResult::maxResonances;

    for (int i = 0; i < count; ++i)
    {
        const float freqHz = snap.resonanceFreqHz[static_cast<size_t> (i)];
        const float q      = snap.resonanceQ[static_cast<size_t> (i)];
        const float gainDb = snap.resonanceGainDb[static_cast<size_t> (i)];

        const juce::String freqStr = freqHz >= 1000.f
            ? juce::String (freqHz / 1000.f, 1) + " kHz"
            : juce::String (static_cast<int> (freqHz)) + " Hz";

        juce::Rectangle<int> slot (plotArea.getX() + static_cast<int> (static_cast<float> (i) * slotW),
                                    area.getY(), static_cast<int> (slotW), area.getHeight());

        g.setColour (juce::Colour (0xff23233c));
        g.fillRoundedRectangle (slot.reduced (2, 4).toFloat(), 3.f);

        g.setFont   (juce::FontOptions (9.5f).withStyle ("Bold"));
        g.setColour (kRefLine);
        g.drawText  (freqStr, slot.withHeight (18).withY (slot.getY() + 5), juce::Justification::centred);

        g.setFont   (juce::FontOptions (8.5f));
        g.setColour (kLabelText);
        g.drawText  ("Q " + juce::String (q, 1) + "  " + juce::String (gainDb, 1) + " dB",
                     slot.withTop (slot.getY() + 23).withHeight (16), juce::Justification::centred);
    }
}

// ── Mastering advice panel ────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::drawAdvicePanel (juce::Graphics& g,
                                                      juce::Rectangle<int> area,
                                                      const AnalysisResult::Snapshot& snap,
                                                      const PresetData& preset) const
{
    g.setColour (juce::Colour (0xff13131f));
    g.fillRect  (area);
    g.setColour (kGridLine);
    g.drawHorizontalLine (area.getY(), 0.f, static_cast<float> (getWidth()));

    // Mirror the meter column layout
    const auto overallPanel = area.withLeft (area.getRight() - kOverallW);
    const auto leftOfPanel  = area.withRight (overallPanel.getX() - kPanelGap);
    const auto scaleCol     = leftOfPanel.withWidth (kScaleW);
    const auto plotArea     = leftOfPanel.withTrimmedLeft (kScaleW);

    // Peak-hold and long-term average are held until transport restarts (resetPeaks).
    // Show placeholder only if no audio has been received since the last reset.
    // peakOverallDb holds its value after transport stops; only resets on next play.
    const float overallMax = (snap.peakOverallDbL + snap.peakOverallDbR) * 0.5f;
    if (overallMax <= -99.f)
    {
        g.setFont   (juce::FontOptions (11.f));
        g.setColour (kDimText);
        g.drawText  ("Play audio to compute mastering recommendations",
                     plotArea, juce::Justification::centred);
        return;
    }

    const auto commonSnap   = buildAnalysisSnapshot (snap, kPercentileWarmupSec);
    const auto commonPreset = toCommonPresetData (preset);
    const auto advice       = audioplugins::common::analysis::deriveAdvice (commonSnap, commonPreset);

    // ── Row geometry (top-down within the panel) ──────────────────────────────
    const int py      = area.getY();
    const int eqTop   = py + 4;
    const int eqH     = 34;          // freq label + bar + gain/Q text
    const int compTop = eqTop + eqH + 4;
    const int compH   = 13;          // each comp text row

    // ── Scale-column labels ───────────────────────────────────────────────────
    g.setFont   (juce::FontOptions (9.0f));
    g.setColour (kDimText);
    g.drawText  ("Mast. EQ",
                 scaleCol.getX(), eqTop, scaleCol.getWidth(), eqH,
                 juce::Justification::centredRight);
    g.drawText  ("MB Comp",
                 scaleCol.getX(), compTop, scaleCol.getWidth(), compH * 2 + 2,
                 juce::Justification::centredRight);

    // ── Per-band recommendations ──────────────────────────────────────────────
    const float slotW = static_cast<float> (plotArea.getWidth()) / BandConfig::numBands;

    // EQ row sub-layout within eqH (34 px):  freq label / bar / gain+Q text
    const int freqLabelH = 11;
    const int barH       = 12;
    const int gainQH     = 11;
    const int barTop     = eqTop + freqLabelH;
    const int gainQTop   = barTop + barH;

    for (size_t i = 0; i < static_cast<size_t> (BandConfig::numBands); ++i)
    {
        const float slotX  = static_cast<float> (plotArea.getX()) + static_cast<float> (i) * slotW;
        const float innerX = slotX + 2.f;
        const float innerW = slotW - 4.f;

        // ── Mastering EQ ──────────────────────────────────────────────────────
        const auto& bandEq = advice.eq[i];
        const float eqGain  = bandEq.gainDb;
        const float absGain = std::abs (eqGain);
        const float q       = bandEq.q;
        const bool  isShelf = bandEq.isShelf;

        // Frequency label
        const float   fHz     = BandConfig::bandCenterHz[i];
        juce::String  freqStr = fHz >= 1000.f
            ? juce::String (fHz / 1000.f, 1) + "k"
            : juce::String ((int) fHz);
        if (isShelf) freqStr += " shelf";

        g.setFont   (juce::FontOptions (9.0f));
        g.setColour (kDimText);
        g.drawText  (freqStr,
                     juce::Rectangle<int> ((int) innerX, eqTop, (int) innerW, freqLabelH),
                     juce::Justification::centred);

        // Center-out bar
        g.setColour (juce::Colour (0xff1e1e32));
        g.fillRoundedRectangle (innerX, static_cast<float> (barTop),
                                innerW, static_cast<float> (barH), 2.f);

        const float cx = innerX + innerW * 0.5f;
        g.setColour (kGridLine);
        g.fillRect  (juce::Rectangle<float> (cx - 0.5f, static_cast<float> (barTop),
                                             1.f, static_cast<float> (barH)));

        if (eqGain != 0.f)
        {
            const float frac  = absGain / 12.f;
            const float barW2 = (innerW * 0.5f - 1.f) * frac;
            g.setColour (eqGain > 0.f ? juce::Colour (0xff33bb55) : juce::Colour (0xffdd8800));
            if (eqGain > 0.f)
                g.fillRoundedRectangle (cx + 1.f, static_cast<float> (barTop + 1),
                                        barW2, static_cast<float> (barH - 2), 2.f);
            else
                g.fillRoundedRectangle (cx - 1.f - barW2, static_cast<float> (barTop + 1),
                                        barW2, static_cast<float> (barH - 2), 2.f);
        }

        // Gain + Q text on the same line
        const juce::String gainStr = eqGain == 0.f
            ? "flat"
            : (eqGain > 0.f ? "+" : "") + juce::String (eqGain, 1) + " dB";
        const juce::String qStr = isShelf || eqGain == 0.f
            ? juce::String()
            : "  Q:" + juce::String (q, 1);

        g.setFont   (juce::FontOptions (9.0f));
        g.setColour (eqGain == 0.f ? kDimText
                     : eqGain > 0.f ? juce::Colour (0xff55dd77)
                                    : juce::Colour (0xffffaa44));
        g.drawText  (gainStr + qStr,
                     juce::Rectangle<int> ((int) innerX, gainQTop, (int) innerW, gainQH),
                     juce::Justification::centred);

        // ── Multiband compression ─────────────────────────────────────────────
        const auto& bandComp  = advice.mbComp[i];
        const float ratio     = bandComp.ratio;
        const float thresh    = bandComp.thresholdDb;
        const float attackMs  = bandComp.attackMs;
        const float releaseMs = bandComp.releaseMs;

        g.setFont   (juce::FontOptions (9.0f));
        g.setColour (kLabelText);

        g.drawText (juce::String ("T:") + juce::String ((int) thresh)
                    + "  " + juce::String (ratio, 1) + ":1",
                    juce::Rectangle<int> ((int) innerX, compTop, (int) innerW, compH),
                    juce::Justification::centred);

        g.drawText (juce::String ("A:") + juce::String ((int) attackMs)
                    + " R:" + juce::String ((int) releaseMs),
                    juce::Rectangle<int> ((int) innerX, compTop + compH, (int) innerW, compH),
                    juce::Justification::centred);
    }

    // ── Mixbus compression panel ──────────────────────────────────────────────
    g.setColour (juce::Colour (0xff1a1a30));
    g.fillRect  (overallPanel);
    g.setColour (kGridLine);
    g.drawVerticalLine (overallPanel.getX(), static_cast<float> (area.getY()),
                        static_cast<float> (area.getBottom()));

    const auto& mixbusComp = advice.mixbusComp;
    const float mbRatio    = mixbusComp.ratio;
    const float mbThresh   = mixbusComp.thresholdDb;
    const float mbAttack   = mixbusComp.attackMs;
    const float mbRelease  = mixbusComp.releaseMs;
    const float makeup     = mixbusComp.makeupDb;

    const int px  = overallPanel.getX() + 4;
    const int pw  = overallPanel.getWidth() - 8;
    int       ry  = area.getY() + 6;
    const int rh  = 14;

    auto drawRow = [&] (const juce::String& label, const juce::String& value)
    {
        g.setFont   (juce::FontOptions (9.0f));
        g.setColour (kDimText);
        g.drawText  (label, px, ry, pw / 2, rh, juce::Justification::centredLeft);
        g.setColour (kLabelText);
        g.drawText  (value, px + pw / 2, ry, pw / 2, rh, juce::Justification::centredRight);
        ry += rh;
    };

    g.setFont   (juce::FontOptions (9.5f).withStyle ("Bold"));
    g.setColour (kLabelText);
    g.drawText  ("Mixbus Comp", px, ry, pw, rh, juce::Justification::centred);
    ry += rh + 2;

    drawRow ("Thresh",  juce::String ((int) mbThresh) + " dB");
    drawRow ("Ratio",   juce::String (mbRatio, 1) + ":1");
    drawRow ("Attack",  juce::String ((int) mbAttack) + " ms");
    drawRow ("Release", juce::String ((int) mbRelease) + " ms");
    drawRow ("Makeup",  "+" + juce::String (makeup, 1) + " dB");

    // ── Loudness / suggested limiter target ───────────────────────────────────
    // LRA shifts the target ±3 LU around a 12 LU neutral point — matches Codex's
    // deriveAdvice() limiter-target formula.
    ry += 2;
    g.setFont   (juce::FontOptions (9.5f).withStyle ("Bold"));
    g.setColour (kLabelText);
    g.drawText  ("Loudness", px, ry, pw, rh, juce::Justification::centred);
    ry += rh + 2;

    const bool  lraReady      = snap.lraLu > 0.f;
    const float limiterTarget = advice.limiter.targetLufsApprox;

    drawRow ("LRA",     lraReady ? juce::String (snap.lraLu, 1) + " LU" : juce::String ("—"));
    drawRow ("Lim Tgt", juce::String (limiterTarget, 1) + " LUFS");
    drawRow ("Ceiling", "-1.0 dBTP");
}

// ── Markdown export ───────────────────────────────────────────────────────────
void MixAdviceAudioProcessorEditor::exportAdvice()
{
    const auto snap    = processorRef.getAnalysisResult().read();
    const auto& preset = processorRef.getPresetManager().getPreset (processorRef.getCurrentProgram());

    const auto commonSnap   = buildAnalysisSnapshot (snap, kPercentileWarmupSec);
    const auto commonPreset = toCommonPresetData (preset);
    auto       advice       = audioplugins::common::analysis::deriveAdvice (commonSnap, commonPreset);
    // deriveAdvice() leaves AdviceSet::resonances empty by design; carry over
    // TrueSight's own live-detected resonances (already shown in the UI's
    // separate drawResonancePanel) so the exported report lists them too.
    advice.resonances = buildResonancePeaks (snap);
    const juce::String md   = juce::String (audioplugins::common::analysis::formatAdviceMarkdown (
        commonSnap, advice, commonPreset, preset.name.toStdString()));

    const juce::String defaultName = juce::String ("MixAdvice_")
        + juce::String (preset.name).replace (" ", "_").replace ("(", "").replace (")", "")
        + ".md";

    fileChooser_ = std::make_unique<juce::FileChooser> (
        "Save Mastering Advice",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (defaultName),
        "*.md");

    fileChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [md] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result != juce::File{})
                result.replaceWithText (md);
        });
}

// ── Correlation colour mapping ────────────────────────────────────────────────
juce::Colour MixAdviceAudioProcessorEditor::corrColour (float corr) noexcept
{
    if (corr > 0.7f)  return juce::Colour (0xff22cc66);  // green  — good
    if (corr > 0.3f)  return juce::Colour (0xffffcc00);  // yellow — caution
    if (corr > 0.0f)  return juce::Colour (0xffff8800);  // orange — problem
    return                    juce::Colour (0xffcc2222);  // red    — cancellation
}
