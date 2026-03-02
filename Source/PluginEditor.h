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

    // ── Simple segmented VU meter (mono) ─────────────────────────────────────
    class LevelMeterComponent : public juce::Component
    {
    public:
        void setLevel (float rmsLinear) noexcept
        {
            const auto dBfs = rmsLinear > 1.0e-6f
                                  ? 20.0f * std::log10 (rmsLinear)
                                  : -60.0f;
            currentLevel = juce::jlimit (0.0f, 1.0f, (dBfs + 60.0f) / 60.0f);
            if (currentLevel >= peakLevel) { peakLevel = currentLevel; peakHold = 45; }
            else if (peakHold > 0) --peakHold;
            else peakLevel = juce::jmax (0.0f, peakLevel - 0.007f);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour::fromRGB (22, 26, 32));
            g.fillRoundedRectangle (b, 4.0f);

            constexpr int segs = 14;
            const float segH = (b.getHeight() - 6.0f) / segs;
            const float segW = b.getWidth() - 6.0f;
            const int lit = juce::roundToInt (currentLevel * segs);

            for (int i = 0; i < segs; ++i)
            {
                const float sy = b.getBottom() - 3.0f - (i + 1) * segH + 1.5f;
                juce::Colour c = (i >= segs - 2) ? juce::Colour::fromRGB (200, 70, 70)
                               : (i >= segs - 4) ? juce::Colour::fromRGB (200, 175, 55)
                                                 : juce::Colour::fromRGB (65, 185, 115);
                g.setColour (i < lit ? c : c.withAlpha (0.14f));
                g.fillRoundedRectangle (b.getX() + 3.0f, sy, segW, segH - 1.5f, 1.5f);
            }

            // peak hold dot
            if (peakLevel > 0.02f)
            {
                const int ps = juce::jlimit (0, segs - 1,
                               juce::roundToInt (peakLevel * segs) - 1);
                const float py = b.getBottom() - 3.0f - (ps + 1) * segH + 1.5f;
                juce::Colour pc = (ps >= segs - 2) ? juce::Colour::fromRGB (220, 90, 90)
                                : (ps >= segs - 4) ? juce::Colour::fromRGB (220, 195, 70)
                                                   : juce::Colour::fromRGB (80, 210, 130);
                g.setColour (pc);
                g.fillRoundedRectangle (b.getX() + 3.0f, py, segW, segH - 1.5f, 1.5f);
            }
        }

    private:
        float currentLevel = 0.0f;
        float peakLevel    = 0.0f;
        int   peakHold     = 0;
    };

    // ── Per-effect spectral card ───────────────────────────────────────────────
    class CardComponent : public juce::Component
    {
    public:
        enum class CardIcon { autoHarm, contrast, saws };

        CardComponent (const juce::String& titleText,
                       juce::Colour accentColour,
                       CardIcon icon);
        void setHarmonicSelectorVisible (bool shouldShow);
        void resized() override;
        void paint (juce::Graphics& g) override;

        // Public so editor can recolour/configure them
        juce::Label        title;
        juce::ToggleButton bypassButton;
        juce::Slider       amountKnob;     // dB / AMP control
        juce::Label        amountLabel;    // "dB"
        juce::ComboBox     harmonicType;
        juce::Slider       wetDryKnob;     // value / intensity control
        juce::Label        wetDryLabel;    // "Value"
        juce::Slider       frequencyRangeSlider;
        juce::Label        frequencyALabel;
        juce::Label        frequencyBLabel;

        juce::Colour accent;
        CardIcon cardIcon;
        bool showHarmonicSelector = true;

    private:
        static void drawAutoHarmIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour col);
        static void drawContrastIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour col);
        static void drawSawsIcon     (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour col);
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
          // Match the warm-cream main panel background
          const auto bg    = juce::Colour::fromRGB (244, 240, 235);
          const auto hover = juce::Colour::fromRGB (235, 231, 225);
          const auto down  = juce::Colour::fromRGB (225, 221, 215);

          g.setColour (isButtonDown ? down : (isMouseOverButton ? hover : bg));
          g.fillRoundedRectangle (bounds, 8.0f);
          g.setColour (juce::Colour::fromRGB (215, 210, 204));
          g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

          if (drawable != nullptr)
          {
            auto iconBounds = bounds.reduced (7.0f).toNearestInt();
            drawable->replaceColour (juce::Colours::black, juce::Colour::fromRGB (100, 96, 92));
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

    // ── Header ────────────────────────────────────────────────────────────────
    juce::Label     pluginNameLabel;
    juce::Label     presetLabel;
    juce::ComboBox  presetSelector;
    IconButton      savePresetButton  { "Save Preset"   };
    IconButton      deletePresetButton{ "Delete Preset" };
    juce::HyperlinkButton versionLabel;
    juce::Label     debugInfoLabel;

    // ── Cards ─────────────────────────────────────────────────────────────────
    CardComponent autoHarmCard { "AutoHarm", juce::Colour::fromRGB (255, 178, 100), CardComponent::CardIcon::autoHarm };
    CardComponent contrastCard { "Contrast", juce::Colour::fromRGB (178, 145, 235), CardComponent::CardIcon::contrast };
    CardComponent sawsCard     { "Saws",     juce::Colour::fromRGB (100, 215, 178), CardComponent::CardIcon::saws };

    // ── Right panel ───────────────────────────────────────────────────────────
    LevelMeterComponent inputLevelMeter;
    LevelMeterComponent outputLevelMeter;
    juce::Label         inputMeterLabel;
    juce::Label         outputMeterLabel;
    juce::Slider        inputGainKnob;
    juce::Slider        outputGainKnob;
    juce::Slider        masterWetDryKnob;
    juce::Label         masterWetDryLabel;

    CognitoniLookAndFeel lookAndFeel;

    // ── APVTS attachments ─────────────────────────────────────────────────────
    std::unique_ptr<SliderAttachment>   autoHarmAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> autoHarmTypeAttachment;
    std::unique_ptr<SliderAttachment>   autoHarmWetDryAttachment;
    std::unique_ptr<ButtonAttachment>   autoHarmBypassAttachment;

    std::unique_ptr<SliderAttachment>   contrastAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> contrastTypeAttachment;
    std::unique_ptr<SliderAttachment>   contrastWetDryAttachment;
    std::unique_ptr<ButtonAttachment>   contrastBypassAttachment;

    std::unique_ptr<SliderAttachment>   sawsAmountAttachment;
    std::unique_ptr<ComboBoxAttachment> sawsTypeAttachment;
    std::unique_ptr<SliderAttachment>   sawsWetDryAttachment;
    std::unique_ptr<ButtonAttachment>   sawsBypassAttachment;

    std::unique_ptr<SliderAttachment>   masterWetDryAttachment;
    std::unique_ptr<SliderAttachment>   inputGainAttachment;
    std::unique_ptr<SliderAttachment>   outputGainAttachment;

    std::atomic<float>* autoHarmMinFreqParam = nullptr;
    std::atomic<float>* autoHarmMaxFreqParam = nullptr;
    std::atomic<float>* contrastMinFreqParam  = nullptr;
    std::atomic<float>* contrastMaxFreqParam  = nullptr;
    std::atomic<float>* sawsMinFreqParam      = nullptr;
    std::atomic<float>* sawsMaxFreqParam      = nullptr;

    bool updatingRangeControls = false;
    int  lastAppliedPresetSelectorId = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CognitoniBlkFxAudioProcessorEditor)
};
