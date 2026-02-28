#include "FFTProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
int overlapToHopSamples (float overlapAmount)
{
    const auto amount = juce::jlimit (0.0f, 1.0f, overlapAmount);
    const auto hop = static_cast<int> (juce::jmap (amount,
                                                   static_cast<float> (FFTProcessor::fftSize - 16),
                                                   static_cast<float> (FFTProcessor::fftSize) * 0.15f));
    return juce::jlimit (32, FFTProcessor::fftSize - 16, hop);
}

std::vector<float> buildTukeyWindow (int size)
{
    // Tukey window (alpha = 0.1): 90% flat rectangular centre with 5% cosine
    // ramp at each edge (taper_samples = alpha/2 * N ≈ 205 for N=4096).
    //
    // Why Tukey instead of Hann:
    //   The original DtBlkFx uses a rectangular analysis window (no windowing
    //   by default, shoulder_frac=0).  This gives the characteristic "blocky",
    //   slightly gritty spectral shape — higher leakage than Hann but that IS
    //   the "flawed" sound the user is looking for.  The short cosine taper
    //   prevents hard-click edges when overlap-adding, and the normRing
    //   per-sample normalisation handles reconstruction correctly regardless of
    //   window shape.
    //
    // With hop=2351 (overlapAmount=0.499) the taper region (205 samples) is
    // always covered by the flat centre of the adjacent frame (overlap=1745),
    // so normRing is always sufficiently large for glitch-free output.
    std::vector<float> window (static_cast<size_t> (size), 1.0f);
    if (size <= 1)
        return window;

    // number of samples in each cosine taper (5% of N on each side)
    const auto taper = static_cast<int> (0.05f * static_cast<float> (size));

    for (int i = 0; i < taper; ++i)
    {
        const auto w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                                    * static_cast<float> (i)
                                                    / static_cast<float> (juce::jmax (1, taper))));
        window[static_cast<size_t> (i)] = w;
        window[static_cast<size_t> (size - 1 - i)] = w;
    }

    return window;
}

float estimateOverlapAddGain (const std::vector<float>& window, int hop)
{
    if (window.empty() || hop <= 0)
        return 1.0f;

    const auto size = static_cast<int> (window.size());
    const auto span = size * 4;
    std::vector<float> accumulation (static_cast<size_t> (span), 0.0f);

    for (int frameStart = 0; frameStart + size < span; frameStart += hop)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto w = window[static_cast<size_t> (i)];
            accumulation[static_cast<size_t> (frameStart + i)] += w * w;
        }
    }

    double meanGain = 0.0;
    int count = 0;
    for (int i = size; i < (2 * size); ++i)
    {
        meanGain += accumulation[static_cast<size_t> (i)];
        ++count;
    }

    if (count <= 0)
        return 1.0f;

    const auto average = static_cast<float> (meanGain / static_cast<double> (count));
    return (average > 1.0e-6f) ? (1.0f / average) : 1.0f;
}
}

FFTProcessor::FFTProcessor()
{
    fftTimeDomain.resize (fftSize, 0.0f);
    fftPacked.resize (2 * fftSize, 0.0f);
    bins.resize ((fftSize / 2) + 1);
    fftWindow = buildTukeyWindow (fftSize);
    overlapAddNormalisation = estimateOverlapAddGain (fftWindow, currentHopSize);
}

void FFTProcessor::prepare (double sampleRate, int numChannels)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    smoothedPowerMatchGain = 1.0f;

    channels.resize (juce::jmax (1, numChannels));
    for (auto& channel : channels)
    {
        channel.inputRing.assign (fftSize, 0.0f);
        channel.outputRing.assign (fftSize, 0.0f);
        channel.normRing.assign (fftSize, 0.0f);
        channel.writePos = 0;
        channel.hopCounter = 0;
    }
}

void FFTProcessor::reset()
{
    for (auto& channel : channels)
    {
        std::fill (channel.inputRing.begin(), channel.inputRing.end(), 0.0f);
        std::fill (channel.outputRing.begin(), channel.outputRing.end(), 0.0f);
        std::fill (channel.normRing.begin(), channel.normRing.end(), 0.0f);
        channel.writePos = 0;
        channel.hopCounter = 0;
    }

    std::fill (fftTimeDomain.begin(), fftTimeDomain.end(), 0.0f);
    std::fill (fftPacked.begin(), fftPacked.end(), 0.0f);
    smoothedPowerMatchGain = 1.0f;
}

void FFTProcessor::setSettings (const Settings& newSettings)
{
    currentHopSize = overlapToHopSamples (newSettings.overlapAmount);
    overlapAddNormalisation = estimateOverlapAddGain (fftWindow, currentHopSize);
}

void FFTProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                 const std::vector<std::unique_ptr<SpectralCard>>& cards)
{
    if (channels.empty() || cards.empty())
        return;

    const auto channelCount = juce::jmin (buffer.getNumChannels(), static_cast<int> (channels.size()));
    const auto numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        auto& state = channels[static_cast<size_t> (channel)];

        for (int sample = 0; sample < numSamples; ++sample)
        {
            state.inputRing[static_cast<size_t> (state.writePos)] = samples[sample];

            // Per-sample OLA normalization: divide accumulated output by the
            // sum of squared window values at this position. This gives exact
            // reconstruction for any window shape + hop size combination,
            // eliminating the 30 Hz amplitude stutter caused by the Tukey
            // taper only partially covering hop-boundary positions.
            const auto rawOutput = state.outputRing[static_cast<size_t> (state.writePos)];
            const auto norm = state.normRing[static_cast<size_t> (state.writePos)];
            state.outputRing[static_cast<size_t> (state.writePos)] = 0.0f;
            state.normRing[static_cast<size_t> (state.writePos)] = 0.0f;
            samples[sample] = (norm > 1.0e-8f) ? rawOutput / norm : 0.0f;

            state.writePos = (state.writePos + 1) % fftSize;
            ++state.hopCounter;

            if (state.hopCounter >= currentHopSize)
            {
                state.hopCounter = 0;
                processFrameForChannel (state, cards);
            }
        }
    }

}

void FFTProcessor::processFrameForChannel (ChannelState& channelState,
                                           const std::vector<std::unique_ptr<SpectralCard>>& cards)
{
    const auto frameStart = channelState.writePos;

    for (int index = 0; index < fftSize; ++index)
    {
        const auto ringIndex = (frameStart + index) % fftSize;
        fftTimeDomain[static_cast<size_t> (index)] = channelState.inputRing[static_cast<size_t> (ringIndex)]
            * fftWindow[static_cast<size_t> (index)];
    }

    std::fill (fftPacked.begin(), fftPacked.end(), 0.0f);
    std::copy (fftTimeDomain.begin(), fftTimeDomain.end(), fftPacked.begin());

    fft.performRealOnlyForwardTransform (fftPacked.data());

    const auto numBins = static_cast<int> (bins.size());
    bins[0] = { fftPacked[0], 0.0f };
    if (numBins > 1)
        bins[static_cast<size_t> (numBins - 1)] = { fftPacked[1], 0.0f };

    for (int bin = 1; bin < numBins - 1; ++bin)
    {
        const auto real = fftPacked[static_cast<size_t> (2 * bin)];
        const auto imag = fftPacked[static_cast<size_t> ((2 * bin) + 1)];
        bins[static_cast<size_t> (bin)] = { real, imag };
    }

    double inputPower = 0.0;
    for (int bin = 0; bin < numBins; ++bin)
        inputPower += std::norm (bins[static_cast<size_t> (bin)]);

    for (const auto& card : cards)
        card->process (bins.data(), numBins);

    double outputPower = 0.0;
    for (int bin = 0; bin < numBins; ++bin)
        outputPower += std::norm (bins[static_cast<size_t> (bin)]);

    if (inputPower > 0.0 && outputPower > 1.0e-30)
    {
        auto powerScale = inputPower / outputPower;
        if (powerScale > 1.0e30)
            powerScale = 1.0;
        if (powerScale < 1.0e-30)
            powerScale = 0.0;

        const auto outScale = static_cast<float> (std::sqrt (powerScale));
        if (std::isfinite (outScale) && outScale >= 0.0f)
        {
            for (int bin = 0; bin < numBins; ++bin)
                bins[static_cast<size_t> (bin)] *= outScale;
        }
    }

    fftPacked[0] = bins[0].real();
    if (numBins > 1)
        fftPacked[1] = bins[static_cast<size_t> (numBins - 1)].real();

    for (int bin = 1; bin < numBins - 1; ++bin)
    {
        fftPacked[static_cast<size_t> (2 * bin)] = bins[static_cast<size_t> (bin)].real();
        fftPacked[static_cast<size_t> ((2 * bin) + 1)] = bins[static_cast<size_t> (bin)].imag();
    }

    fft.performRealOnlyInverseTransform (fftPacked.data());

    for (int index = 0; index < fftSize; ++index)
    {
        const auto w = fftWindow[static_cast<size_t> (index)];
        // Synthesis: apply analysis window (symmetric A=S Tukey pair).
        // Do NOT apply overlapAddNormalisation here — normalization is now
        // handled per-sample via normRing, which tracks sum(w²) at each
        // output position, giving perfect reconstruction for any hop size.
        const auto outputSample = fftPacked[static_cast<size_t> (index)] * w;
        const auto ringIndex = static_cast<size_t> ((frameStart + index) % fftSize);

        channelState.outputRing[ringIndex] += outputSample;
        channelState.normRing[ringIndex] += w * w;
    }
}

void FFTProcessor::applyPowerMatch (juce::AudioBuffer<float>& processed,
                                    const juce::AudioBuffer<float>& dryReference)
{
    juce::ignoreUnused (processed, dryReference);
}

