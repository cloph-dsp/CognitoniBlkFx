// FFTProcessor — spectral processing engine, port of DtBlkFx (Darrell Tam, 2005-2006).

#include "FFTProcessor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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
}

// ─────────────────────────────────────────────────────────────────────────────
FFTProcessor::FFTProcessor()
{
    fftTimeDomain.resize (fftSize, 0.0f);
    fftPacked.resize (2 * fftSize, 0.0f);
    bins.resize (static_cast<size_t> (fftSize / 2 + 1));
}

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::prepare (double sampleRate, int numChannels)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);

    channels.resize (static_cast<size_t> (juce::jmax (1, numChannels)));
    for (auto& ch : channels)
    {
        ch.inputRing.assign (fftSize, 0.0f);
        ch.outputRing.assign (outputRingSize, 0.0f);
        ch.inputWritePos  = 0;
        ch.outputReadPos  = 0;
        ch.outputWritePos = fftSize;
        ch.hopCounter     = 0;
        ch.powerScale     = 1.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.inputRing.begin(),  ch.inputRing.end(),  0.0f);
        std::fill (ch.outputRing.begin(), ch.outputRing.end(), 0.0f);
        ch.inputWritePos  = 0;
        ch.outputReadPos  = 0;
        ch.outputWritePos = fftSize;
        ch.hopCounter     = 0;
        ch.powerScale     = 1.0f;
    }
    std::fill (fftTimeDomain.begin(), fftTimeDomain.end(), 0.0f);
    std::fill (fftPacked.begin(),     fftPacked.end(),     0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::setSettings (const Settings& newSettings)
{
    currentHopSize = overlapToHopSamples (newSettings.overlapAmount);
}

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                 const std::vector<std::unique_ptr<SpectralCard>>& cards)
{
    if (channels.empty() || cards.empty())
        return;

    const auto channelCount = juce::jmin (buffer.getNumChannels(),
                                          static_cast<int> (channels.size()));
    const auto numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        auto& ch      = channels[static_cast<size_t> (channel)];

        for (int s = 0; s < numSamples; ++s)
        {
            ch.inputRing[static_cast<size_t> (ch.inputWritePos % fftSize)] = samples[s];

            const auto readIdx = static_cast<size_t> (ch.outputReadPos % outputRingSize);
            samples[s] = ch.outputRing[readIdx];
            ch.outputRing[readIdx] = 0.0f;

            ++ch.inputWritePos;
            ++ch.outputReadPos;
            ++ch.hopCounter;

            if (ch.hopCounter >= currentHopSize)
            {
                ch.hopCounter = 0;
                processFrameForChannel (ch, cards);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void FFTProcessor::processFrameForChannel (ChannelState& ch,
                                           const std::vector<std::unique_ptr<SpectralCard>>& cards)
{
    const int numBins = static_cast<int> (bins.size());

    const int readStart = ch.inputWritePos - fftSize;
    for (int i = 0; i < fftSize; ++i)
    {
        const int src = ((readStart + i) % fftSize + fftSize) % fftSize;
        fftTimeDomain[static_cast<size_t> (i)] = ch.inputRing[static_cast<size_t> (src)];
    }

    std::fill (fftPacked.begin(), fftPacked.end(), 0.0f);
    std::copy (fftTimeDomain.begin(), fftTimeDomain.end(), fftPacked.begin());
    fft.performRealOnlyForwardTransform (fftPacked.data());

    bins[0] = { fftPacked[0], 0.0f };
    bins[static_cast<size_t> (numBins - 1)] = { fftPacked[1], 0.0f };
    for (int b = 1; b < numBins - 1; ++b)
    {
        bins[static_cast<size_t> (b)] = { fftPacked[static_cast<size_t> (2 * b)],
                                          fftPacked[static_cast<size_t> (2 * b + 1)] };
    }

    double inputPower = 0.0;
    for (int b = 0; b < numBins; ++b)
        inputPower += static_cast<double> (std::norm (bins[static_cast<size_t> (b)]));

    for (const auto& card : cards)
        card->process (bins.data(), numBins);

    // Power normalisation — port of DtBlkFx::procFFT (pwr_match=1, mixback=0):
    // out_scale = sqrt(total_in_pwr / out_pwr), applied directly each frame.
    // No smoothing: the time-domain crossfade handles amplitude continuity.
    double outputPower = 0.0;
    for (int b = 0; b < numBins; ++b)
        outputPower += static_cast<double> (std::norm (bins[static_cast<size_t> (b)]));

    if (inputPower > 1.0e-30 && outputPower > 1.0e-30)
    {
        double rawScale2 = inputPower / outputPower;
        if (rawScale2 > 1.0e30) rawScale2 = 1.0;
        if (rawScale2 < 1.0e-30) rawScale2 = 0.0;
        ch.powerScale = static_cast<float> (std::sqrt (rawScale2));
    }

    for (int b = 0; b < numBins; ++b)
        bins[static_cast<size_t> (b)] *= ch.powerScale;

    fftPacked[0] = bins[0].real();
    fftPacked[1] = bins[static_cast<size_t> (numBins - 1)].real();
    for (int b = 1; b < numBins - 1; ++b)
    {
        fftPacked[static_cast<size_t> (2 * b)]     = bins[static_cast<size_t> (b)].real();
        fftPacked[static_cast<size_t> (2 * b + 1)] = bins[static_cast<size_t> (b)].imag();
    }
    fft.performRealOnlyInverseTransform (fftPacked.data());

    // Write to output ring — port of DtBlkFx::mixToX3:
    //   overlap region: linear crossfade (old→new)
    //   mid section:    blatant copy
    const int overlapLen = fftSize - currentHopSize;
    const int outStart   = ch.outputWritePos;

    for (int i = 0; i < overlapLen; ++i)
    {
        const float t   = static_cast<float> (i) / static_cast<float> (juce::jmax (1, overlapLen - 1));
        const int pos   = (outStart + i) % outputRingSize;
        ch.outputRing[static_cast<size_t> (pos)] =
            ch.outputRing[static_cast<size_t> (pos)] * (1.0f - t)
            + fftPacked[static_cast<size_t> (i)] * t;
    }

    for (int i = overlapLen; i < fftSize; ++i)
    {
        const int pos = (outStart + i) % outputRingSize;
        ch.outputRing[static_cast<size_t> (pos)] = fftPacked[static_cast<size_t> (i)];
    }

    ch.outputWritePos = (outStart + currentHopSize) % outputRingSize;
}
