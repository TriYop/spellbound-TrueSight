#include "MasteringAdvicePanel.h"
#include "Analysis/BandConfig.h"
#include <cstdio>

MasteringAdvicePanel::MasteringAdvicePanel(DGL_NAMESPACE::NanoTopLevelWidget* const parent)
    : DGL_NAMESPACE::NanoSubWidget(parent)
{
}

void MasteringAdvicePanel::update(const audioplugins::common::analysis::AdviceSet& advice,
                                   const std::vector<audioplugins::common::analysis::ResonancePeak>& resonances,
                                   const float lraLu)
{
    advice_ = advice;
    resonances_ = resonances;
    lraLu_ = lraLu;
    repaint();
}

void MasteringAdvicePanel::onNanoDisplay()
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    if (w <= 0.f || h <= 0.f) return;

    beginPath();
    rect(0.f, 0.f, w, h);
    fillColor(DGL_NAMESPACE::Color(0x13, 0x13, 0x1f, 1.0f));
    fill();
    closePath();

    fontSize(9.0f);
    textAlign(ALIGN_LEFT | ALIGN_TOP);

    const float slotW = w / static_cast<float>(BandConfig::numBands);
    char line[64];

    for (size_t i = 0; i < static_cast<size_t>(BandConfig::numBands); ++i)
    {
        const auto& eq = advice_.eq[i];
        const float x = static_cast<float>(i) * slotW + 4.f;

        std::snprintf(line, sizeof(line), "%.0fHz", BandConfig::bandCenterHz[i]);
        beginPath();
        fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f));
        text(x, 4.f, line, nullptr);
        closePath();

        std::snprintf(line, sizeof(line), eq.gainDb == 0.f ? "flat" : "%+.1fdB", eq.gainDb);
        beginPath();
        fillColor(eq.gainDb == 0.f ? DGL_NAMESPACE::Color(0xaa, 0xaa, 0xaa, 1.0f)
                                    : eq.gainDb > 0.f ? DGL_NAMESPACE::Color(0x55, 0xdd, 0x77, 1.0f)
                                                       : DGL_NAMESPACE::Color(0xff, 0xaa, 0x44, 1.0f));
        text(x, 18.f, line, nullptr);
        closePath();
    }

    const auto& mb = advice_.mixbusComp;
    std::snprintf(line, sizeof(line), "Mixbus: T:%.0f  %.1f:1  A:%.0fms R:%.0fms  Mkp:+%.1fdB",
                  mb.thresholdDb, mb.ratio, mb.attackMs, mb.releaseMs, mb.makeupDb);
    beginPath();
    fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f));
    text(4.f, 36.f, line, nullptr);
    closePath();

    if (lraLu_ > 0.f)
        std::snprintf(line, sizeof(line), "LRA: %.1f LU   Limiter target: %.1f LUFS   Ceiling: -1.0 dBTP",
                      lraLu_, advice_.limiter.targetLufsApprox);
    else
        std::snprintf(line, sizeof(line), "LRA: --   Limiter target: %.1f LUFS   Ceiling: -1.0 dBTP",
                      advice_.limiter.targetLufsApprox);
    beginPath();
    fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f));
    text(4.f, 50.f, line, nullptr);
    closePath();

    float ry = 68.f;
    for (const auto& peak : resonances_)
    {
        std::snprintf(line, sizeof(line), "Cut: %.0fHz  Q:%.1f  %.1fdB", peak.freqHz, peak.q, peak.gainDb);
        beginPath();
        fillColor(DGL_NAMESPACE::Color(0xcc, 0xcc, 0xcc, 1.0f));
        text(4.f, ry, line, nullptr);
        closePath();
        ry += 12.f;
    }
}
