#include "AutoHarmCard.h"
#include "HarmonicMaskModel.h"
#include "SpectralMaskModel.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
float effectAmpMultiplier (float ampParam, bool mixMode)
{
    const auto p = juce::jlimit (0.0f, 1.0f, ampParam);
    constexpr auto zeroDbParam = 0.6f;

    if (mixMode && p < zeroDbParam)
        return p * (1.0f / zeroDbParam);

    const auto db = juce::jmap (p, -60.0f, 40.0f);
    return (p <= 0.0f) ? 0.0f : std::pow (10.0f, db * 0.05f);
}
}

AutoHarmCard::Settings AutoHarmCard::PresetManager::getProfile (Profile profile)
{
    Settings settings;

    switch (profile)
    {
        case Profile::DtBlkFxClassicAutoHarm:
        default:
            settings.common.isBypassed = false;
            settings.common.wetDry = 1.0f;
            settings.targetIntensity = 0.3f;
            settings.harmonicType = SpectralHarmonicType::Odd;
            settings.searchBandHz.minHz = 0.0f;
            settings.searchBandHz.maxHz = 40000.0f;
            break;
    }

    return settings;
}

void AutoHarmCard::setSettings (const Settings& newSettings)
{
    setCommonSettings (newSettings.common);
    targetIntensity.store (juce::jlimit (0.0f, 1.0f, newSettings.targetIntensity), std::memory_order_relaxed);
    harmonicType.store (juce::jlimit (0, 3, static_cast<int> (newSettings.harmonicType)), std::memory_order_relaxed);
    frequencyAHz.store (juce::jmax (0.0f, newSettings.searchBandHz.minHz), std::memory_order_relaxed);
    frequencyBHz.store (juce::jmax (0.0f, newSettings.searchBandHz.maxHz), std::memory_order_relaxed);
}

AutoHarmCard::Settings AutoHarmCard::getSettingsSnapshot() const
{
    Settings snapshot;
    snapshot.common = getCommonSettingsSnapshot();
    snapshot.targetIntensity = targetIntensity.load (std::memory_order_relaxed);
    snapshot.harmonicType = static_cast<SpectralHarmonicType> (juce::jlimit (0, 3, harmonicType.load (std::memory_order_relaxed)));
    snapshot.searchBandHz.minHz = frequencyAHz.load (std::memory_order_relaxed);
    snapshot.searchBandHz.maxHz = frequencyBHz.load (std::memory_order_relaxed);
    return snapshot;
}

void AutoHarmCard::setProcessingContext (double sampleRate, int fftSize)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    currentFftSize = juce::jmax (2, fftSize);
}

void AutoHarmCard::process (juce::dsp::Complex<float>* bins, int numBins)
{
    if (bins == nullptr || numBins <= 1)
        return;

    const auto settings = getSettingsSnapshot();
    if (settings.common.isBypassed)
        return;

    const auto amp = effectAmpMultiplier (settings.common.wetDry, false);
    if (amp <= 0.0f)
        return;

    const auto value = juce::jlimit (0.0f, 1.0f, settings.targetIntensity);
    if (value <= 0.0f)
        return;

    // Map our SpectralHarmonicType enum to the original DtBlkFx harmonic-type
    // index used inside HarmonicMaskModel::buildGeometry (SplitParam<4> encoding):
    //   Original 0 = all harmonics  (our Both = 2)
    //   Original 1 = odd harmonics  (our Odd  = 0)
    //   Original 2 = even harmonics (our Even = 1)
    //   Original 3 = between        (our Between = 3)
    // The targetIntensity (value) controls the harmonic WIDTH (0..99%) within
    // the selected type.  Synthesising the packed value this way makes the
    // Type dropdown actually work instead of being a no-op.
    static constexpr int harmTypeMap[4] = { 1, 2, 0, 3 }; // Odd, Even, Both, Between
    const auto uiTypeIndex = juce::jlimit (0, 3, static_cast<int> (settings.harmonicType));
    const auto originalTypeIndex = harmTypeMap[uiTypeIndex];
    const auto packedHarmValue = (static_cast<float> (originalTypeIndex) + value * 0.9999f) / 4.0f;

    const auto hzPerBin = static_cast<float> (currentSampleRate / static_cast<double> (currentFftSize));
    if (hzPerBin <= 0.0f)
        return;

    // Copied/ported algorithmic structure from DtBlkFx lineage (Darrell Tam; refactor lineage by skullzy):
    // - peak/fundamental estimation in the first 1/8 of the spectrum
    // - harmonic geometry from packed harmonic value (both/odd/even/between + width)
    // - bounded harmonic count for mask generation
    const auto minBin = 1;
    const auto maxBin = juce::jlimit (minBin, numBins - 2, (numBins - 1) / 8);
    const auto fundamentalBin = HarmonicMaskModel::findPeakOrFundamentalBin (bins,
                                                                              numBins,
                                                                              minBin,
                                                                              maxBin,
                                                                              5.0f);

    const auto maskGeometry = HarmonicMaskModel::buildGeometry (packedHarmValue,
                                                                fundamentalBin,
                                                                numBins,
                                                                1.0f,
                                                                200);
    if (! maskGeometry.isValid())
        return;

    std::vector<uint8_t> harmonicMask (static_cast<size_t> (numBins), 0);
    HarmonicMaskModel::forEachMaskedRange (maskGeometry, 1, numBins - 1,
                                           [&harmonicMask] (int v0, int v1)
                                           {
                                               for (int bin = v0; bin <= v1; ++bin)
                                                   harmonicMask[static_cast<size_t> (bin)] = 1;
                                           });

    std::vector<uint8_t> inBandMask;
    SpectralMaskModel::buildInBandMask (inBandMask,
                                        numBins,
                                        hzPerBin,
                                        settings.searchBandHz,
                                        1);

    for (int bin = 1; bin < numBins; ++bin)
    {
        if (inBandMask[static_cast<size_t> (bin)] == 0)
            continue;

        if (harmonicMask[static_cast<size_t> (bin)] != 0)
            bins[bin] *= amp;
    }
}
