#pragma once

#include <JuceHeader.h>
#include "../Cards/SpectralCard.h"

class FFTProcessor
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    struct Settings
    {
        // 0.88 → hop ≈ 1024 = N/4 at N=4096, which is 75 % overlap.
        // Hann window + 75 % overlap satisfies COLA precisely.
        float overlapAmount = 0.88f;
    };

    FFTProcessor();

    void prepare (double sampleRate, int numChannels);
    void reset();
    void setSettings (const Settings& newSettings);
    void processBlock (juce::AudioBuffer<float>& buffer,
                       const std::vector<std::unique_ptr<SpectralCard>>& cards);

private:
    struct ChannelState
    {
        std::vector<float> inputRing;
        std::vector<float> outputRing;
        // Per-sample OLA normalisation ring: tracks sum of w²(n-kH) at each
        // position. Dividing outputRing by normRing gives perfect reconstruction
        // regardless of window shape or hop size, eliminating the amplitude
        // oscillation (stutter) that occurs when the short Tukey taper only
        // partially covers some hop-boundary positions.
        std::vector<float> normRing;
        int writePos = 0;
        int hopCounter = 0;
    };

    void processFrameForChannel (ChannelState& channelState,
                                 const std::vector<std::unique_ptr<SpectralCard>>& cards);

    void applyPowerMatch (juce::AudioBuffer<float>& processed,
                          const juce::AudioBuffer<float>& dryReference);

    double currentSampleRate = 0.0;
    int currentHopSize = fftSize / 8;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fftTimeDomain;
    std::vector<float> fftPacked;
    std::vector<juce::dsp::Complex<float>> bins;
    std::vector<float> fftWindow;
    float overlapAddNormalisation = 1.0f;
    float smoothedPowerMatchGain = 1.0f;

    std::vector<ChannelState> channels;
};
