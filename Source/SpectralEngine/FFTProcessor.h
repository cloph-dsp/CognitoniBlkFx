#pragma once

#include <JuceHeader.h>
#include "../Cards/SpectralCard.h"

class FFTProcessor
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;  // 4096

    static constexpr int outputRingSize = 3 * fftSize;

    struct Settings
    {
        float overlapAmount = 0.499f;
    };

    FFTProcessor();

    void prepare (double sampleRate, int numChannels);
    void reset();
    void setSettings (const Settings& newSettings);
    void processBlock (juce::AudioBuffer<float>& buffer,
                       const std::vector<std::unique_ptr<SpectralCard>>& cards);

private:
    // -------------------------------------------------------------------------
    struct ChannelState
    {
        std::vector<float> inputRing;
        std::vector<float> outputRing;
        int inputWritePos  = 0;
        int outputReadPos  = 0;
        int outputWritePos = 0;
        int hopCounter     = 0;
        float powerScale   = 1.0f;
    };

    void processFrameForChannel (ChannelState& channelState,
                                 const std::vector<std::unique_ptr<SpectralCard>>& cards);

    double currentSampleRate = 0.0;
    int    currentHopSize    = fftSize / 2;

    juce::dsp::FFT                          fft { fftOrder };
    std::vector<float>                      fftTimeDomain;
    std::vector<float>                      fftPacked;
    std::vector<juce::dsp::Complex<float>>  bins;

    std::vector<ChannelState> channels;
};
