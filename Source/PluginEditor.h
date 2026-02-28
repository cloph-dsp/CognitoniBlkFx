/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class CognitoniBlkFxAudioProcessorEditor  : public juce::AudioProcessorEditor
                                           , private juce::Timer
{
public:
    CognitoniBlkFxAudioProcessorEditor (CognitoniBlkFxAudioProcessor&);
    ~CognitoniBlkFxAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    class CardComponent : public juce::Component
    {
    public:
      CardComponent (const juce::String& titleText,
               const juce::String& amountLabelText,
               juce::Colour backgroundColour,
               juce::Colour accentColour);
        void setHarmonicSelectorVisible (bool shouldShow);
        void resized() override;
        void paint (juce::Graphics& g) override;

        juce::Label title;
        juce::ToggleButton bypassButton;
        juce::Slider amountKnob;
        juce::Label amountLabel;
        juce::ComboBox harmonicType;
        juce::Slider wetDryKnob;
        juce::Label wetDryLabel;
        juce::Slider frequencyRangeSlider;
        juce::Label frequencyALabel;
        juce::Label frequencyBLabel;

        juce::Colour cardBackground;
        juce::Colour accent;
        bool showHarmonicSelector = true;
    };

      class IconButton : public juce::Button
      {
      public:
        IconButton (const juce::String& buttonName)
          : juce::Button (buttonName) {}

        void setSvgIcon (const juce::String& svgText)
        {
            auto svgXml = juce::parseXML (svgText);
            if (svgXml != nullptr)
                drawable = juce::Drawable::createFromSVG (*svgXml);
        }

        void paintButton (juce::Graphics& g,
                  bool isMouseOverButton,
                  bool isButtonDown) override
        {
          auto bounds = getLocalBounds().toFloat().reduced (0.5f);
          const auto base = juce::Colour::fromRGB (220, 225, 230);
          const auto hover = juce::Colour::fromRGB (210, 217, 224);
          const auto down = juce::Colour::fromRGB (198, 207, 216);

          g.setColour (isButtonDown ? down : (isMouseOverButton ? hover : base));
          g.fillRoundedRectangle (bounds, 8.0f);
          g.setColour (juce::Colour::fromRGB (164, 173, 181));
          g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

          if (drawable != nullptr)
          {
            auto iconBounds = bounds.reduced (7.0f).toNearestInt();
            drawable->replaceColour (juce::Colours::black, juce::Colour::fromRGB (29, 35, 42));
            drawable->drawWithin (g, iconBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
          }
        }

      private:
        std::unique_ptr<juce::Drawable> drawable;
      };

      class CognitoniLookAndFeel final : public juce::LookAndFeel_V4
      {
      public:
        CognitoniLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                     float sliderPosProportional, float rotaryStartAngle,
                     float rotaryEndAngle, juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                     float sliderPos, float minSliderPos, float maxSliderPos,
                     juce::Slider::SliderStyle, juce::Slider&) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                   int buttonX, int buttonY, int buttonW, int buttonH,
                   juce::ComboBox&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                   bool shouldDrawButtonAsDown) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;
      };

    static void configureAmountKnob (juce::Slider& slider);
      static void configureRangeSlider (juce::Slider& slider);
    juce::String normalisedToHzText (double normalisedValue) const;
      static juce::String normalisedToPercentText (double normalisedValue);
      void syncRangeSlidersFromParams();
      void pushRangeSliderToParams();
      void setParameterNormalised (const juce::String& parameterId, float value);
    void timerCallback() override;
    void refreshFrequencyLabels();
    void refreshPresetSelectorItems();
    void showSavePresetDialog();
    void showDeletePresetDialog();

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    CognitoniBlkFxAudioProcessor& audioProcessor;

    juce::Label presetLabel;
    juce::ComboBox presetSelector;
    IconButton savePresetButton { "Save Preset" };
    IconButton deletePresetButton { "Delete Preset" };
    juce::Label versionLabel;
    juce::Label debugInfoLabel;
    CardComponent autoHarmCard {
      "AutoHarm", "Amplitude",
      juce::Colour::fromRGB (227, 236, 248),
      juce::Colour::fromRGB (74, 143, 214)
    };
    CardComponent contrastCard {
      "Contrast", "Amplitude",
      juce::Colour::fromRGB (241, 231, 247),
      juce::Colour::fromRGB (170, 112, 214)
    };
    CardComponent sawsCard {
      "Saws", "Amplitude",
      juce::Colour::fromRGB (245, 237, 225),
      juce::Colour::fromRGB (205, 134, 77)
    };
    juce::Slider masterWetDryKnob;
    juce::Label masterWetDryLabel;
    CognitoniLookAndFeel lookAndFeel;

    std::unique_ptr<SliderAttachment> autoHarmAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> autoHarmTypeAttachment;
    std::unique_ptr<SliderAttachment> autoHarmWetDryAttachment;
    std::unique_ptr<ButtonAttachment> autoHarmBypassAttachment;

    std::unique_ptr<SliderAttachment> contrastAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> contrastTypeAttachment;
    std::unique_ptr<SliderAttachment> contrastWetDryAttachment;
    std::unique_ptr<ButtonAttachment> contrastBypassAttachment;

    std::unique_ptr<SliderAttachment> sawsAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> sawsTypeAttachment;
    std::unique_ptr<SliderAttachment> sawsWetDryAttachment;
    std::unique_ptr<ButtonAttachment> sawsBypassAttachment;

    std::unique_ptr<SliderAttachment> masterWetDryAttachment;

    std::atomic<float>* autoHarmMinFreqParam = nullptr;
    std::atomic<float>* autoHarmMaxFreqParam = nullptr;
    std::atomic<float>* contrastMinFreqParam = nullptr;
    std::atomic<float>* contrastMaxFreqParam = nullptr;
    std::atomic<float>* sawsMinFreqParam = nullptr;
    std::atomic<float>* sawsMaxFreqParam = nullptr;

    bool updatingRangeControls = false;
    int lastAppliedPresetSelectorId = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CognitoniBlkFxAudioProcessorEditor)
};
