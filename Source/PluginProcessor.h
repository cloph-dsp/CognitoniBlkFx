/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Cards/AutoHarmCard.h"
#include "Cards/ContrastCard.h"
#include "Cards/SawsCard.h"
#include "CardSchema.h"
#include "SpectralEngine/FFTProcessor.h"

#include <atomic>
#include <memory>
#include <vector>

//==============================================================================
/**
*/
class CognitoniBlkFxAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    CognitoniBlkFxAudioProcessor();
    ~CognitoniBlkFxAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    float getCurrentNyquistHz() const noexcept;
    std::pair<float, float> getAutoHarmSearchBandHzForUi() const noexcept;
    juce::StringArray getPresetNames() const;
    void applyPresetByIndex (int presetIndex);
    bool saveCurrentPresetAs (const juce::String& presetName);
    bool deletePresetByIndex (int presetIndex);
    bool isPresetUserDeletable (int presetIndex) const noexcept;
    int getCurrentPresetIndex() const noexcept;
    float getLastInputRms() const noexcept;
    float getLastOutputRms() const noexcept;
    int getLastSanitisedSamples() const noexcept;
    int getLastInputChannels() const noexcept;
    int getLastOutputChannels() const noexcept;

private:
  struct CardRuntimeParameters
  {
    std::atomic<float>* amount = nullptr;
    std::atomic<float>* type = nullptr;
    std::atomic<float>* freqA = nullptr;
    std::atomic<float>* freqB = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* wetDry = nullptr;
  };

    struct PresetParameterValue
    {
      juce::String parameterId;
      float plainValue = 0.0f;
    };

    struct PresetDefinition
    {
      juce::String name;
      bool userPreset = false;
      std::vector<PresetParameterValue> globalValues;
      struct CardPresetDefinition
      {
          CardSchema::CardId cardId = CardSchema::CardId::autoHarm;
          std::vector<PresetParameterValue> values;
      };
      std::vector<CardPresetDefinition> cards;
    };

    const std::vector<PresetDefinition>& getPresetDefinitions() const;
    static std::vector<PresetDefinition> createDefaultPresetDefinitions();
    bool loadPresetsFromJson();
    bool savePresetsToJson() const;
    juce::File getPresetStorageFile() const;
    static juce::var serializePresetToJson (const PresetDefinition& preset);
    static bool deserializePresetFromJson (const juce::var& jsonValue, PresetDefinition& outPreset);
    void setParameterToPlainValue (const juce::String& parameterId, float plainValue);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void initialiseCardRack();
    void pushParameterSnapshotToCards();
    void bindRuntimeParameters();
    CardRuntimeParameters& runtimeParamsFor (CardSchema::CardId cardId) noexcept;
    const CardRuntimeParameters& runtimeParamsFor (CardSchema::CardId cardId) const noexcept;
    void applyMasterWetDryMix (juce::AudioBuffer<float>& wetBuffer,
                               const juce::AudioBuffer<float>& dryBuffer) const;

    juce::AudioProcessorValueTreeState apvts;

    std::array<CardRuntimeParameters, 3> cardRuntimeParams;
    std::atomic<float>* masterWetDryParam = nullptr;
    std::atomic<float>* inputGainParam    = nullptr;
    std::atomic<float>* outputGainParam   = nullptr;

    FFTProcessor fftProcessor;
    std::vector<std::unique_ptr<SpectralCard>> cardRack;

    AutoHarmCard* autoHarmCard = nullptr;
    ContrastCard* contrastCard = nullptr;
    SawsCard* sawsCard = nullptr;
    std::vector<PresetDefinition> presetDefinitions;
    std::atomic<int> currentPresetIndex { 0 };
    std::atomic<float> lastInputRms { 0.0f };
    std::atomic<float> lastOutputRms { 0.0f };
    std::atomic<int> lastSanitisedSamples { 0 };
    std::atomic<int> lastInputChannels { 0 };
    std::atomic<int> lastOutputChannels { 0 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CognitoniBlkFxAudioProcessor)
};
