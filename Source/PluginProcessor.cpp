/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cmath>
#include <functional>

namespace
{
constexpr auto& autoHarmParams = CardSchema::paramsFor (CardSchema::CardId::autoHarm);
constexpr auto& contrastParams = CardSchema::paramsFor (CardSchema::CardId::contrast);
constexpr auto& sawsParams = CardSchema::paramsFor (CardSchema::CardId::saws);

constexpr auto paramAutoHarmIntensity = autoHarmParams.amount;
constexpr auto paramAutoHarmType = autoHarmParams.type;
constexpr auto paramAutoHarmMinFreq = autoHarmParams.freqA;
constexpr auto paramAutoHarmMaxFreq = autoHarmParams.freqB;
constexpr auto paramAutoHarmBypass = autoHarmParams.bypass;
constexpr auto paramAutoHarmWetDry = autoHarmParams.wetDry;

constexpr auto paramContrastAmount = contrastParams.amount;
constexpr auto paramContrastType = contrastParams.type;
constexpr auto paramContrastMinFreq = contrastParams.freqA;
constexpr auto paramContrastMaxFreq = contrastParams.freqB;
constexpr auto paramContrastBypass = contrastParams.bypass;
constexpr auto paramContrastWetDry = contrastParams.wetDry;

constexpr auto paramSawsAmount = sawsParams.amount;
constexpr auto paramSawsType = sawsParams.type;
constexpr auto paramSawsMinFreq = sawsParams.freqA;
constexpr auto paramSawsMaxFreq = sawsParams.freqB;
constexpr auto paramSawsBypass = sawsParams.bypass;
constexpr auto paramSawsWetDry = sawsParams.wetDry;

constexpr auto paramMasterWetDry = "masterWetDry";
constexpr auto paramInputGain    = "inputGain";
constexpr auto paramOutputGain   = "outputGain";

constexpr float dtBlkC0Hz = 16.3516f;
constexpr float dtBlkNoteSpan = 255.0f * 0.5f;

float loadParamOr (const std::atomic<float>* parameter, float fallback) noexcept
{
    return (parameter != nullptr) ? parameter->load (std::memory_order_relaxed) : fallback;
}

float normalisedToDtBlkHz (float normalised, float nyquistHz) noexcept
{
    const auto v = juce::jlimit (0.0f, 1.0f, normalised);
    if (v <= 0.0f)
        return 0.0f;

    const auto noteOffset = v * dtBlkNoteSpan;
    const auto hz = dtBlkC0Hz * std::pow (2.0f, noteOffset / 12.0f);
    return juce::jlimit (0.0f, juce::jmax (20.0f, nyquistHz), hz);
}
}

std::vector<CognitoniBlkFxAudioProcessor::PresetDefinition> CognitoniBlkFxAudioProcessor::createDefaultPresetDefinitions()
{
    auto makeCardValues = [] (CardSchema::CardId cardId,
                              std::initializer_list<std::pair<CardSchema::Key, float>> entries)
    {
        PresetDefinition::CardPresetDefinition card;
        card.cardId = cardId;
        for (const auto& entry : entries)
            card.values.push_back ({ CardSchema::paramIdFor (cardId, entry.first), entry.second });
        return card;
    };

    std::vector<PresetDefinition> defaults
    {
        {
            "Empty",
            false,
            {
                { paramMasterWetDry, 1.0f }
            },
            {
                makeCardValues (CardSchema::CardId::autoHarm,
                                {
                                    { CardSchema::Key::bypass, 1.0f },
                                    { CardSchema::Key::wetDry, 1.0f },
                                    { CardSchema::Key::amount, 0.0f },
                                    { CardSchema::Key::type, 0.0f },
                                    { CardSchema::Key::freqA, 0.0f },
                                    { CardSchema::Key::freqB, 1.0f }
                                }),
                makeCardValues (CardSchema::CardId::contrast,
                                {
                                    { CardSchema::Key::bypass, 1.0f },
                                    { CardSchema::Key::wetDry, 1.0f },
                                    { CardSchema::Key::amount, 0.0f },
                                    { CardSchema::Key::type, 0.0f },
                                    { CardSchema::Key::freqA, 0.0f },
                                    { CardSchema::Key::freqB, 1.0f }
                                }),
                makeCardValues (CardSchema::CardId::saws,
                                {
                                    { CardSchema::Key::bypass, 1.0f },
                                    { CardSchema::Key::wetDry, 1.0f },
                                    { CardSchema::Key::amount, 0.0f },
                                    { CardSchema::Key::type, 2.0f },
                                    { CardSchema::Key::freqA, 0.0f },
                                    { CardSchema::Key::freqB, 1.0f }
                                })
            }
        },
        {
            "AutoHarm",
            false,
            {
                { paramMasterWetDry, 1.0f }
            },
            {
                makeCardValues (CardSchema::CardId::autoHarm,
                                {
                                    { CardSchema::Key::bypass, 0.0f },
                                    // AutoHarm AMP = 1.0 → 40 dB (max). The original preset's
                                    // harmonic amplitude is at maximum.
                                    { CardSchema::Key::wetDry, 1.0f },
                                    // SET0.FX_VAL=0.325 in original preset → SplitParam<4>: i_part=1 (odd), f_part=0.3 (30% width)
                                    { CardSchema::Key::amount, 0.325f },
                                    { CardSchema::Key::type, 0.0f },
                                    { CardSchema::Key::freqA, 0.245f },  // SET0.FREQ_A=0.245 → ~100 Hz
                                    { CardSchema::Key::freqB, 1.0f }     // SET0.FREQ_B=1.0 → Nyquist
                                }),
                makeCardValues (CardSchema::CardId::contrast,
                                {
                                    { CardSchema::Key::bypass, 0.0f },
                                    // Contrast AMP=0.598 → -0.2 dB (original SET1.AMP)
                                    { CardSchema::Key::wetDry, 0.598f },
                                    { CardSchema::Key::amount, 0.3005f },
                                    { CardSchema::Key::type, 0.0f },
                                    { CardSchema::Key::freqA, 0.0f },
                                    { CardSchema::Key::freqB, 1.0f }
                                }),
                makeCardValues (CardSchema::CardId::saws,
                                {
                                    { CardSchema::Key::bypass, 1.0f },
                                    { CardSchema::Key::wetDry, 1.0f },
                                    { CardSchema::Key::amount, 0.45f },
                                    { CardSchema::Key::type, 2.0f },
                                    { CardSchema::Key::freqA, 0.0f },
                                    { CardSchema::Key::freqB, 1.0f }
                                })
            }
        }
    };

    return defaults;
}

const std::vector<CognitoniBlkFxAudioProcessor::PresetDefinition>& CognitoniBlkFxAudioProcessor::getPresetDefinitions() const
{
    return presetDefinitions;
}

//==============================================================================
CognitoniBlkFxAudioProcessor::CognitoniBlkFxAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
#else
        : apvts (*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    bindRuntimeParameters();
    masterWetDryParam = apvts.getRawParameterValue (paramMasterWetDry);
    inputGainParam    = apvts.getRawParameterValue (paramInputGain);
    outputGainParam   = apvts.getRawParameterValue (paramOutputGain);

    presetDefinitions = createDefaultPresetDefinitions();
    loadPresetsFromJson();

    initialiseCardRack();
    applyPresetByIndex (0);
}

CognitoniBlkFxAudioProcessor::~CognitoniBlkFxAudioProcessor()
{
}

void CognitoniBlkFxAudioProcessor::bindRuntimeParameters()
{
    auto bindCard = [this] (CardSchema::CardId cardId)
    {
        const auto& ids = CardSchema::paramsFor (cardId);
        auto& runtime = runtimeParamsFor (cardId);
        runtime.amount = apvts.getRawParameterValue (ids.amount);
        runtime.type = apvts.getRawParameterValue (ids.type);
        runtime.freqA = apvts.getRawParameterValue (ids.freqA);
        runtime.freqB = apvts.getRawParameterValue (ids.freqB);
        runtime.bypass = apvts.getRawParameterValue (ids.bypass);
        runtime.wetDry = apvts.getRawParameterValue (ids.wetDry);
    };

    bindCard (CardSchema::CardId::autoHarm);
    bindCard (CardSchema::CardId::contrast);
    bindCard (CardSchema::CardId::saws);
}

CognitoniBlkFxAudioProcessor::CardRuntimeParameters& CognitoniBlkFxAudioProcessor::runtimeParamsFor (CardSchema::CardId cardId) noexcept
{
    return cardRuntimeParams[static_cast<size_t> (cardId)];
}

const CognitoniBlkFxAudioProcessor::CardRuntimeParameters& CognitoniBlkFxAudioProcessor::runtimeParamsFor (CardSchema::CardId cardId) const noexcept
{
    return cardRuntimeParams[static_cast<size_t> (cardId)];
}

//==============================================================================
const juce::String CognitoniBlkFxAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CognitoniBlkFxAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CognitoniBlkFxAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CognitoniBlkFxAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CognitoniBlkFxAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CognitoniBlkFxAudioProcessor::getNumPrograms()
{
    return juce::jmax (1, static_cast<int> (getPresetDefinitions().size()));
}

int CognitoniBlkFxAudioProcessor::getCurrentProgram()
{
    return currentPresetIndex.load (std::memory_order_relaxed);
}

void CognitoniBlkFxAudioProcessor::setCurrentProgram (int index)
{
    applyPresetByIndex (index);
}

const juce::String CognitoniBlkFxAudioProcessor::getProgramName (int index)
{
    const auto& presets = getPresetDefinitions();
    if (index >= 0 && index < static_cast<int> (presets.size()))
        return presets[static_cast<size_t> (index)].name;

    return "Preset";
}

void CognitoniBlkFxAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    if (newName.isEmpty())
        return;

    if (index < 0 || index >= static_cast<int> (presetDefinitions.size()))
        return;

    presetDefinitions[static_cast<size_t> (index)].name = newName;
    savePresetsToJson();
}

juce::StringArray CognitoniBlkFxAudioProcessor::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& preset : getPresetDefinitions())
        names.add (preset.name);
    return names;
}

bool CognitoniBlkFxAudioProcessor::saveCurrentPresetAs (const juce::String& presetName)
{
    if (presetName.isEmpty())
        return false;

    PresetDefinition newPreset;
    newPreset.name = presetName;
    newPreset.userPreset = true;
    newPreset.globalValues =
    {
        { paramMasterWetDry, (masterWetDryParam != nullptr) ? masterWetDryParam->load (std::memory_order_relaxed) : 1.0f }
    };

    auto captureCard = [this] (CardSchema::CardId cardId)
    {
        PresetDefinition::CardPresetDefinition card;
        card.cardId = cardId;

        const auto& ids = CardSchema::paramsFor (cardId);
        std::array<const char*, 6> ordered
        {
            ids.bypass, ids.wetDry, ids.amount, ids.type, ids.freqA, ids.freqB
        };

        for (const auto* id : ordered)
        {
            if (auto* value = apvts.getRawParameterValue (id))
                card.values.push_back ({ id, value->load (std::memory_order_relaxed) });
        }

        return card;
    };

    newPreset.cards.push_back (captureCard (CardSchema::CardId::autoHarm));
    newPreset.cards.push_back (captureCard (CardSchema::CardId::contrast));
    newPreset.cards.push_back (captureCard (CardSchema::CardId::saws));

    presetDefinitions.push_back (newPreset);
    currentPresetIndex.store (static_cast<int> (presetDefinitions.size()) - 1, std::memory_order_relaxed);
    return savePresetsToJson();
}

bool CognitoniBlkFxAudioProcessor::deletePresetByIndex (int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= static_cast<int> (presetDefinitions.size()))
        return false;

    auto& preset = presetDefinitions[static_cast<size_t> (presetIndex)];
    if (! preset.userPreset)
        return false;

    presetDefinitions.erase (presetDefinitions.begin() + presetIndex);

    if (presetDefinitions.empty())
        presetDefinitions = createDefaultPresetDefinitions();

    auto nextIndex = 0;
    for (int i = 0; i < static_cast<int> (presetDefinitions.size()); ++i)
    {
        if (presetDefinitions[static_cast<size_t> (i)].name == "Empty")
        {
            nextIndex = i;
            break;
        }
    }

    applyPresetByIndex (nextIndex);
    return savePresetsToJson();
}

bool CognitoniBlkFxAudioProcessor::isPresetUserDeletable (int presetIndex) const noexcept
{
    if (presetIndex < 0 || presetIndex >= static_cast<int> (presetDefinitions.size()))
        return false;

    return presetDefinitions[static_cast<size_t> (presetIndex)].userPreset;
}

int CognitoniBlkFxAudioProcessor::getCurrentPresetIndex() const noexcept
{
    return currentPresetIndex.load (std::memory_order_relaxed);
}

float CognitoniBlkFxAudioProcessor::getLastInputRms() const noexcept
{
    return lastInputRms.load (std::memory_order_relaxed);
}

float CognitoniBlkFxAudioProcessor::getLastOutputRms() const noexcept
{
    return lastOutputRms.load (std::memory_order_relaxed);
}

int CognitoniBlkFxAudioProcessor::getLastSanitisedSamples() const noexcept
{
    return lastSanitisedSamples.load (std::memory_order_relaxed);
}

void CognitoniBlkFxAudioProcessor::setParameterToPlainValue (const juce::String& parameterId, float plainValue)
{
    if (auto* parameter = apvts.getParameter (parameterId))
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            parameter->setValueNotifyingHost (ranged->convertTo0to1 (plainValue));
        else
            parameter->setValueNotifyingHost (plainValue);
    }
}

void CognitoniBlkFxAudioProcessor::applyPresetByIndex (int presetIndex)
{
    const auto& presets = getPresetDefinitions();
    if (presets.empty())
        return;

    const auto clamped = juce::jlimit (0, static_cast<int> (presets.size()) - 1, presetIndex);
    const auto& preset = presets[static_cast<size_t> (clamped)];

    if (auto beginGesture = [this] (const juce::String& parameterId)
    {
        if (auto* parameter = apvts.getParameter (parameterId))
            parameter->beginChangeGesture();
    }; true)
    {
        for (const auto& value : preset.globalValues)
        {
            beginGesture (value.parameterId);
            setParameterToPlainValue (value.parameterId, value.plainValue);
        }
    }

    for (const auto& cardPreset : preset.cards)
    {
        juce::ignoreUnused (cardPreset.cardId);
        for (const auto& value : cardPreset.values)
        {
            if (auto* parameter = apvts.getParameter (value.parameterId))
                parameter->beginChangeGesture();
            setParameterToPlainValue (value.parameterId, value.plainValue);
        }
    }

    for (const auto& value : preset.globalValues)
        if (auto* parameter = apvts.getParameter (value.parameterId))
            parameter->endChangeGesture();

    for (const auto& cardPreset : preset.cards)
        for (const auto& value : cardPreset.values)
            if (auto* parameter = apvts.getParameter (value.parameterId))
                parameter->endChangeGesture();

    currentPresetIndex.store (clamped, std::memory_order_relaxed);
    pushParameterSnapshotToCards();
}

//==============================================================================
void CognitoniBlkFxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    const auto prepareChannels = juce::jmax (1, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    fftProcessor.prepare (sampleRate, prepareChannels);

    // Report the fftSize latency introduced by the crossfade output approach.
    // Mirrors original DtBlkFx: output ring (x3) is always fftSize ahead of input.
    setLatencySamples (FFTProcessor::fftSize);

    FFTProcessor::Settings fftSettings;
    // 0.499 overlap: hop=2351 for N=4096 (42.6% overlap).
    // Matches original DtBlkFx AutoHarm preset (OVERLAP=0.499).
    // Larger hop = wider blocks in spectrogram = original's "blocky" feel.
    fftSettings.overlapAmount = 0.499f;
    fftProcessor.setSettings (fftSettings);

    for (auto& card : cardRack)
        card->setProcessingContext (sampleRate, FFTProcessor::fftSize);

    pushParameterSnapshotToCards();
}

void CognitoniBlkFxAudioProcessor::releaseResources()
{
    fftProcessor.reset();
}

int CognitoniBlkFxAudioProcessor::getLastInputChannels() const noexcept
{
    return lastInputChannels.load (std::memory_order_relaxed);
}

int CognitoniBlkFxAudioProcessor::getLastOutputChannels() const noexcept
{
    return lastOutputChannels.load (std::memory_order_relaxed);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CognitoniBlkFxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void CognitoniBlkFxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const auto bufferChannels = buffer.getNumChannels();

    lastInputChannels.store (totalNumInputChannels, std::memory_order_relaxed);
    lastOutputChannels.store (totalNumOutputChannels, std::memory_order_relaxed);

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    if (totalNumInputChannels > 0 && totalNumOutputChannels > totalNumInputChannels)
    {
        const auto clearFrom = juce::jlimit (0, bufferChannels, totalNumInputChannels);
        const auto clearTo = juce::jlimit (clearFrom, bufferChannels, totalNumOutputChannels);
        for (auto channel = clearFrom; channel < clearTo; ++channel)
            buffer.clear (channel, 0, buffer.getNumSamples());
    }

    // Input gain — apply before spectral processing so the dry copy also has the gain
    const auto inputGainDb  = loadParamOr (inputGainParam, 0.0f);
    const auto inputGainLin = juce::Decibels::decibelsToGain (inputGainDb, -60.0f);
    if (std::abs (inputGainLin - 1.0f) > 1.0e-5f)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGain (ch, 0, buffer.getNumSamples(), inputGainLin);

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf (buffer, true);

    const auto cardIsActive = [this] (CardSchema::CardId cardId)
    {
        const auto& params = runtimeParamsFor (cardId);
        const auto bypassed = loadParamOr (params.bypass, 0.0f) > 0.5f;
        const auto wet = loadParamOr (params.wetDry, 1.0f);
        return (! bypassed) && (wet > 1.0e-4f);
    };

    const auto hasActiveCard = cardIsActive (CardSchema::CardId::autoHarm)
        || cardIsActive (CardSchema::CardId::contrast)
        || cardIsActive (CardSchema::CardId::saws);

    if (! hasActiveCard)
    {
        applyMasterWetDryMix (buffer, dryBuffer);

        // Output gain in bypass path too
        const auto ogDb = loadParamOr (outputGainParam, 0.0f);
        const auto ogLin = juce::Decibels::decibelsToGain (ogDb, -60.0f);
        if (std::abs (ogLin - 1.0f) > 1.0e-5f)
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.applyGain (ch, 0, buffer.getNumSamples(), ogLin);

        double energy = 0.0;
        const auto channelsToUse = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());
        const auto samplesToUse = juce::jmin (buffer.getNumSamples(), dryBuffer.getNumSamples());
        for (int channel = 0; channel < channelsToUse; ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);
            for (int sample = 0; sample < samplesToUse; ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                energy += value * value;
            }
        }

        const auto denom = static_cast<double> (juce::jmax (1, channelsToUse * samplesToUse));
        const auto rms = static_cast<float> (std::sqrt (energy / denom));
        lastInputRms.store (rms, std::memory_order_relaxed);
        lastOutputRms.store (rms, std::memory_order_relaxed);
        lastSanitisedSamples.store (0, std::memory_order_relaxed);
        return;
    }

    FFTProcessor::Settings fftSettings;
    fftSettings.overlapAmount = 0.499f;
    fftProcessor.setSettings (fftSettings);

    pushParameterSnapshotToCards();
    fftProcessor.processBlock (buffer, cardRack);

    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    const auto channelsToUse = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());
    const auto samplesToUse = juce::jmin (buffer.getNumSamples(), dryBuffer.getNumSamples());

    int sanitisedSamples = 0;

    for (int channel = 0; channel < channelsToUse; ++channel)
    {
        const auto* in = dryBuffer.getReadPointer (channel);
        auto* out = buffer.getWritePointer (channel);

        for (int sample = 0; sample < samplesToUse; ++sample)
        {
            const auto inSample = static_cast<double> (in[sample]);
            auto outSample = static_cast<double> (out[sample]);

            if (! std::isfinite (outSample))
            {
                out[sample] = in[sample];
                outSample = static_cast<double> (in[sample]);
                ++sanitisedSamples;
            }

            inputEnergy += inSample * inSample;
            outputEnergy += outSample * outSample;
        }
    }

    applyMasterWetDryMix (buffer, dryBuffer);

    // Output gain — applied after the wet/dry blend
    const auto outputGainDb  = loadParamOr (outputGainParam, 0.0f);
    const auto outputGainLin = juce::Decibels::decibelsToGain (outputGainDb, -60.0f);
    if (std::abs (outputGainLin - 1.0f) > 1.0e-5f)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGain (ch, 0, buffer.getNumSamples(), outputGainLin);

    double mixedEnergy = 0.0;
    for (int channel = 0; channel < channelsToUse; ++channel)
    {
        const auto* mixed = buffer.getReadPointer (channel);
        for (int sample = 0; sample < samplesToUse; ++sample)
        {
            const auto value = static_cast<double> (mixed[sample]);
            mixedEnergy += value * value;
        }
    }

    const auto denom = static_cast<double> (juce::jmax (1, channelsToUse * samplesToUse));
    lastInputRms.store (static_cast<float> (std::sqrt (inputEnergy / denom)), std::memory_order_relaxed);
    lastOutputRms.store (static_cast<float> (std::sqrt (mixedEnergy / denom)), std::memory_order_relaxed);
    lastSanitisedSamples.store (sanitisedSamples, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout CognitoniBlkFxAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.reserve (21);

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramMasterWetDry, 1 },
        "Master WetDry",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramInputGain, 1 },
        "Input Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramOutputGain, 1 },
        "Output Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { paramAutoHarmBypass, 1 },
        "AutoHarm Bypass",
        false));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramAutoHarmWetDry, 1 },
        "AutoHarm WetDry",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramAutoHarmIntensity, 1 },
        "AutoHarm Target Intensity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.3f));

    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { paramAutoHarmType, 1 },
        "AutoHarm Harmonic Type",
        juce::StringArray { "Odd", "Even", "Both", "Between" },
        0));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramAutoHarmMinFreq, 1 },
        "AutoHarm Min Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramAutoHarmMaxFreq, 1 },
        "AutoHarm Max Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramContrastAmount, 1 },
        "Contrast Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.45f));

    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { paramContrastType, 1 },
        "Contrast Harmonic Type",
        juce::StringArray { "Odd", "Even", "Both", "Between" },
        0));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramContrastMinFreq, 1 },
        "Contrast Min Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramContrastMaxFreq, 1 },
        "Contrast Max Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { paramContrastBypass, 1 },
        "Contrast Bypass",
        false));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramContrastWetDry, 1 },
        "Contrast WetDry",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramSawsAmount, 1 },
        "Saws Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.62f));

    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { paramSawsType, 1 },
        "Saws Harmonic Type",
        juce::StringArray { "Odd", "Even", "Both", "Between" },
        2));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramSawsMinFreq, 1 },
        "Saws Min Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramSawsMaxFreq, 1 },
        "Saws Max Frequency",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    parameters.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { paramSawsBypass, 1 },
        "Saws Bypass",
        true));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { paramSawsWetDry, 1 },
        "Saws WetDry",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f));

    return { parameters.begin(), parameters.end() };
}

float CognitoniBlkFxAudioProcessor::getCurrentNyquistHz() const noexcept
{
    const auto sr = getSampleRate();
    return (sr > 0.0) ? static_cast<float> (sr * 0.5) : 22050.0f;
}

std::pair<float, float> CognitoniBlkFxAudioProcessor::getAutoHarmSearchBandHzForUi() const noexcept
{
    const auto nyquist = getCurrentNyquistHz();
    const auto& autoParams = runtimeParamsFor (CardSchema::CardId::autoHarm);
    const auto freqANorm = juce::jlimit (0.0f, 1.0f, loadParamOr (autoParams.freqA, 0.0f));
    const auto freqBNorm = juce::jlimit (0.0f, 1.0f, loadParamOr (autoParams.freqB, 1.0f));

    return { normalisedToDtBlkHz (freqANorm, nyquist), normalisedToDtBlkHz (freqBNorm, nyquist) };
}

void CognitoniBlkFxAudioProcessor::applyMasterWetDryMix (juce::AudioBuffer<float>& wetBuffer,
                                                         const juce::AudioBuffer<float>& dryBuffer) const
{
    float wet = 1.0f;
    if (masterWetDryParam != nullptr)
    {
        wet = juce::jlimit (0.0f, 1.0f, masterWetDryParam->load (std::memory_order_relaxed));
    }
    else if (auto* parameter = apvts.getParameter (paramMasterWetDry))
    {
        wet = juce::jlimit (0.0f, 1.0f, parameter->getValue());
    }

    if (wet >= 1.0f)
        return;

    if (wet <= 0.0f)
    {
        wetBuffer.makeCopyOf (dryBuffer, true);
        return;
    }

    const auto dry = 1.0f - wet;
    const auto channelsToUse = juce::jmin (wetBuffer.getNumChannels(), dryBuffer.getNumChannels());
    const auto samplesToUse = juce::jmin (wetBuffer.getNumSamples(), dryBuffer.getNumSamples());

    for (int channel = 0; channel < channelsToUse; ++channel)
    {
        auto* wetData = wetBuffer.getWritePointer (channel);
        const auto* dryData = dryBuffer.getReadPointer (channel);

        for (int sample = 0; sample < samplesToUse; ++sample)
            wetData[sample] = (wetData[sample] * wet) + (dryData[sample] * dry);
    }
}

void CognitoniBlkFxAudioProcessor::initialiseCardRack()
{
    using CardFactory = std::function<std::unique_ptr<SpectralCard>()>;

    const std::array<CardFactory, 3> factories
    {
        [] { return std::make_unique<AutoHarmCard>(); },
        [] { return std::make_unique<ContrastCard>(); },
        [] { return std::make_unique<SawsCard>(); }
    };

    cardRack.clear();
    cardRack.reserve (factories.size());

    for (const auto& factory : factories)
        cardRack.push_back (factory());

    for (auto& card : cardRack)
    {
        if (auto* autoCard = dynamic_cast<AutoHarmCard*> (card.get()))
            autoHarmCard = autoCard;

        if (auto* contrast = dynamic_cast<ContrastCard*> (card.get()))
            contrastCard = contrast;

        if (auto* saws = dynamic_cast<SawsCard*> (card.get()))
            sawsCard = saws;
    }

    if (autoHarmCard != nullptr)
        autoHarmCard->setSettings (AutoHarmCard::PresetManager::getProfile (AutoHarmCard::Profile::DtBlkFxClassicAutoHarm));
}

void CognitoniBlkFxAudioProcessor::pushParameterSnapshotToCards()
{
    if (autoHarmCard != nullptr)
    {
        const auto& params = runtimeParamsFor (CardSchema::CardId::autoHarm);
        auto settings = AutoHarmCard::PresetManager::getProfile (AutoHarmCard::Profile::DtBlkFxClassicAutoHarm);
        const auto nyquist = juce::jmax (20.0f, static_cast<float> (getSampleRate() * 0.5));

        settings.common.isBypassed = loadParamOr (params.bypass, 0.0f) > 0.5f;
        settings.common.wetDry = loadParamOr (params.wetDry, 1.0f);
        settings.targetIntensity = juce::jlimit (0.0f, 1.0f, loadParamOr (params.amount, 0.0f));
        settings.harmonicType = static_cast<SpectralHarmonicType> (
            juce::jlimit (0, 3, static_cast<int> (loadParamOr (params.type, 0.0f))));

        const auto freqANorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqA, 0.0f));
        const auto freqBNorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqB, 1.0f));

        settings.searchBandHz.minHz = normalisedToDtBlkHz (freqANorm, nyquist);
        settings.searchBandHz.maxHz = normalisedToDtBlkHz (freqBNorm, nyquist);

        autoHarmCard->setSettings (settings);
    }

    if (contrastCard != nullptr)
    {
        const auto& params = runtimeParamsFor (CardSchema::CardId::contrast);
        ContrastCard::Settings settings;
        const auto nyquist = juce::jmax (20.0f, static_cast<float> (getSampleRate() * 0.5));

        settings.common.isBypassed = loadParamOr (params.bypass, 0.0f) > 0.5f;
        settings.common.wetDry = loadParamOr (params.wetDry, 1.0f);
        settings.amount = juce::jlimit (0.0f, 1.0f, loadParamOr (params.amount, 0.0f));
        settings.harmonicType = static_cast<SpectralHarmonicType> (
            juce::jlimit (0, 3, static_cast<int> (loadParamOr (params.type, 0.0f))));

        const auto freqANorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqA, 0.0f));
        const auto freqBNorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqB, 1.0f));

        settings.searchBandHz.minHz = normalisedToDtBlkHz (freqANorm, nyquist);
        settings.searchBandHz.maxHz = normalisedToDtBlkHz (freqBNorm, nyquist);

        contrastCard->setSettings (settings);
    }

    if (sawsCard != nullptr)
    {
        const auto& params = runtimeParamsFor (CardSchema::CardId::saws);
        SawsCard::Settings settings;
        const auto nyquist = juce::jmax (20.0f, static_cast<float> (getSampleRate() * 0.5));

        settings.common.isBypassed = loadParamOr (params.bypass, 1.0f) > 0.5f;
        settings.common.wetDry = loadParamOr (params.wetDry, 1.0f);
        settings.amount = juce::jlimit (0.0f, 1.0f, loadParamOr (params.amount, 0.0f));
        settings.harmonicType = static_cast<SpectralHarmonicType> (
            juce::jlimit (0, 3, static_cast<int> (loadParamOr (params.type, 2.0f))));

        const auto freqANorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqA, 0.0f));
        const auto freqBNorm = juce::jlimit (0.0f, 1.0f, loadParamOr (params.freqB, 1.0f));

        settings.searchBandHz.minHz = normalisedToDtBlkHz (freqANorm, nyquist);
        settings.searchBandHz.maxHz = normalisedToDtBlkHz (freqBNorm, nyquist);

        sawsCard->setSettings (settings);
    }
}

//==============================================================================
bool CognitoniBlkFxAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CognitoniBlkFxAudioProcessor::createEditor()
{
    return new CognitoniBlkFxAudioProcessorEditor (*this);
}

//==============================================================================
void CognitoniBlkFxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void CognitoniBlkFxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto vt = juce::ValueTree::fromXml (*xml);
        if (vt.isValid() && vt.hasType (apvts.state.getType()))
            apvts.replaceState (vt);
    }
}

juce::File CognitoniBlkFxAudioProcessor::getPresetStorageFile() const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("CognitoniBlkFx");

    if (! directory.exists())
        directory.createDirectory();

    return directory.getChildFile ("presets.json");
}

juce::var CognitoniBlkFxAudioProcessor::serializePresetToJson (const PresetDefinition& preset)
{
    auto* presetObj = new juce::DynamicObject();
    presetObj->setProperty ("name", preset.name);
    presetObj->setProperty ("userPreset", preset.userPreset);

    auto* globalObj = new juce::DynamicObject();
    for (const auto& global : preset.globalValues)
        globalObj->setProperty (global.parameterId, global.plainValue);
    presetObj->setProperty ("global", juce::var (globalObj));

    auto* cardsObj = new juce::DynamicObject();
    for (const auto& cardPreset : preset.cards)
    {
        auto* cardObj = new juce::DynamicObject();
        for (const auto& value : cardPreset.values)
        {
            for (int k = 0; k <= static_cast<int> (CardSchema::Key::wetDry); ++k)
            {
                const auto key = static_cast<CardSchema::Key> (k);
                if (value.parameterId == CardSchema::paramIdFor (cardPreset.cardId, key))
                {
                    cardObj->setProperty (CardSchema::keyName (key), value.plainValue);
                    break;
                }
            }
        }

        cardsObj->setProperty (CardSchema::cardName (cardPreset.cardId), juce::var (cardObj));
    }

    presetObj->setProperty ("cards", juce::var (cardsObj));
    return juce::var (presetObj);
}

bool CognitoniBlkFxAudioProcessor::deserializePresetFromJson (const juce::var& jsonValue, PresetDefinition& outPreset)
{
    auto* presetObj = jsonValue.getDynamicObject();
    if (presetObj == nullptr)
        return false;

    outPreset = {};
    outPreset.name = presetObj->getProperty ("name").toString();
    outPreset.userPreset = static_cast<bool> (presetObj->getProperty ("userPreset"));
    if (outPreset.name.isEmpty())
        return false;

    if (auto* globalObj = presetObj->getProperty ("global").getDynamicObject())
    {
        const auto& props = globalObj->getProperties();
        for (int i = 0; i < props.size(); ++i)
        {
            PresetParameterValue value;
            value.parameterId = props.getName (i).toString();
            value.plainValue = static_cast<float> (props.getValueAt (i));
            outPreset.globalValues.push_back (value);
        }
    }

    auto parseCard = [&outPreset, presetObj] (const juce::String& cardName, CardSchema::CardId cardId)
    {
        auto* cardsObj = presetObj->getProperty ("cards").getDynamicObject();
        if (cardsObj == nullptr)
            return;

        auto* cardObj = cardsObj->getProperty (cardName).getDynamicObject();
        if (cardObj == nullptr)
            return;

        PresetDefinition::CardPresetDefinition card;
        card.cardId = cardId;

        const auto& props = cardObj->getProperties();
        for (int i = 0; i < props.size(); ++i)
        {
            const auto keyVar = props.getName (i).toString();
            const auto key = CardSchema::keyFromName (keyVar);
            PresetParameterValue value;
            value.parameterId = CardSchema::paramIdFor (cardId, key);
            value.plainValue = static_cast<float> (props.getValueAt (i));
            card.values.push_back (value);
        }

        outPreset.cards.push_back (card);
    };

    parseCard ("AutoHarm", CardSchema::CardId::autoHarm);
    parseCard ("Contrast", CardSchema::CardId::contrast);
    parseCard ("Saws", CardSchema::CardId::saws);

    return ! outPreset.cards.empty();
}

bool CognitoniBlkFxAudioProcessor::loadPresetsFromJson()
{
    const auto file = getPresetStorageFile();
    if (! file.existsAsFile())
        return savePresetsToJson();

    const auto parsed = juce::JSON::parse (file);
    if (parsed.isVoid())
        return false;

    juce::Array<juce::var>* presetsArray = nullptr;
    if (auto* rootObj = parsed.getDynamicObject())
    {
        if (auto* arr = rootObj->getProperty ("presets").getArray())
            presetsArray = arr;
    }
    else if (parsed.isArray())
    {
        presetsArray = parsed.getArray();
    }

    if (presetsArray == nullptr)
        return false;

    std::vector<PresetDefinition> loaded;
    for (const auto& value : *presetsArray)
    {
        PresetDefinition preset;
        if (deserializePresetFromJson (value, preset))
        {
            if (preset.userPreset)
            {
                loaded.push_back (std::move (preset));
                continue;
            }

            if (! (preset.name == "AutoHarm"
                   || preset.name == "Empty"))
            {
                preset.userPreset = true;
                loaded.push_back (std::move (preset));
            }
        }
    }

    if (! loaded.empty())
        presetDefinitions.insert (presetDefinitions.end(), loaded.begin(), loaded.end());

    return ! presetDefinitions.empty();
}

bool CognitoniBlkFxAudioProcessor::savePresetsToJson() const
{
    auto* rootObj = new juce::DynamicObject();
    juce::Array<juce::var> presetsArray;

    for (const auto& preset : presetDefinitions)
    {
        if (preset.userPreset)
            presetsArray.add (serializePresetToJson (preset));
    }

    rootObj->setProperty ("presets", presetsArray);
    return getPresetStorageFile().replaceWithText (juce::JSON::toString (juce::var (rootObj), true));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CognitoniBlkFxAudioProcessor();
}
