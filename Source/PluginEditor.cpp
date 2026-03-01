/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Icons/EmbeddedIcons.h"

#include <cmath>

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

constexpr double dtBlkC0Hz = 16.3516;
constexpr double dtBlkNoteSpan = 255.0 * 0.5;

double normalisedToDtBlkHzUi (double normalised, double nyquistHz)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v <= 0.0)
        return 0.0;

    const auto noteOffset = v * dtBlkNoteSpan;
    const auto hz = dtBlkC0Hz * std::pow (2.0, noteOffset / 12.0);
    return juce::jlimit (0.0, juce::jmax (20.0, nyquistHz), hz);
}

juce::String autoHarmValueText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    const auto scaled = v * 4.0;
    const auto part = juce::jlimit (0, 3, static_cast<int> (std::floor (scaled)));
    const auto frac = juce::jlimit (0.0, 0.999, scaled - static_cast<double> (part));
    const auto pct = static_cast<int> (std::round (frac * 100.0));

    switch (part)
    {
        case 0: return juce::String (pct) + "% Both";
        case 1: return juce::String (pct) + "% Odd";
        case 2: return juce::String (pct) + "% Even";
        default: return juce::String (pct) + "% Between";
    }
}

juce::String contrastValueText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    const auto signedPercent = static_cast<int> (std::round ((v * 200.0) - 100.0));
    return juce::String (signedPercent) + "%";
}

juce::String sawsValueText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v < 0.5)
    {
        const auto pct = static_cast<int> (std::round ((v * 2.0) * 100.0));
        return "scale" + juce::String (pct);
    }

    const auto pct = static_cast<int> (std::round (((v - 0.5) * 2.0) * 100.0));
    return "copy" + juce::String (pct);
}

// Converts a normalised 0..1 parameter to a dB string matching the original DtBlkFx
// FX_AMP display: lin_interp(param, -60, +40) dB (Darrell Tam).
juce::String normalisedToDbText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v <= 0.0)
        return "-inf dB";
    const auto db = v * 100.0 - 60.0;
    return juce::String (db, 1) + " dB";
}

}

CognitoniBlkFxAudioProcessorEditor::CardComponent::CardComponent (const juce::String& titleText,
                                                                                                                                    const juce::String& amountLabelText,
                                                                                                                                    juce::Colour backgroundColour,
                                                                                                                                    juce::Colour accentColour)
        : cardBackground (backgroundColour),
            accent (accentColour)
{
    addAndMakeVisible (title);
    title.setText (titleText, juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (juce::FontOptions().withHeight (34.0f).withStyle ("SemiBold"));

    addAndMakeVisible (bypassButton);
    bypassButton.setButtonText (" ");

    addAndMakeVisible (amountKnob);

    addAndMakeVisible (amountLabel);
    amountLabel.setText (amountLabelText, juce::dontSendNotification);
    amountLabel.setJustificationType (juce::Justification::centred);
    amountLabel.setFont (juce::FontOptions().withHeight (20.0f).withStyle ("SemiBold"));

    addAndMakeVisible (harmonicType);
    harmonicType.addItem ("Odd", 1);
    harmonicType.addItem ("Even", 2);
    harmonicType.addItem ("Both", 3);
    harmonicType.addItem ("Between", 4);

    addAndMakeVisible (wetDryKnob);
    addAndMakeVisible (wetDryLabel);
    wetDryLabel.setText ("Value", juce::dontSendNotification);
    wetDryLabel.setJustificationType (juce::Justification::centred);
    wetDryLabel.setFont (juce::FontOptions().withHeight (19.0f).withStyle ("SemiBold"));

    addAndMakeVisible (frequencyRangeSlider);
    addAndMakeVisible (frequencyALabel);
    addAndMakeVisible (frequencyBLabel);
    frequencyALabel.setText ("Start", juce::dontSendNotification);
    frequencyBLabel.setText ("End", juce::dontSendNotification);
    frequencyALabel.setJustificationType (juce::Justification::centredLeft);
    frequencyBLabel.setJustificationType (juce::Justification::centredRight);
    frequencyALabel.setFont (juce::FontOptions().withHeight (15.0f));
    frequencyBLabel.setFont (juce::FontOptions().withHeight (15.0f));
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.11f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 5.0f), 20.0f);

    g.setGradientFill (juce::ColourGradient (cardBackground.brighter (0.10f), bounds.getTopLeft(),
                                             cardBackground.darker (0.02f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, 20.0f);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 20.0f, 2.0f);

    g.setColour (accent.withAlpha (0.30f));
    g.drawRoundedRectangle (bounds.reduced (8.0f), 16.0f, 1.0f);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::setHarmonicSelectorVisible (bool shouldShow)
{
    showHarmonicSelector = shouldShow;
    harmonicType.setVisible (shouldShow);
    harmonicType.setEnabled (shouldShow);
    resized();
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::resized()
{
    auto area = getLocalBounds().reduced (14);

    const auto contentHeight = showHarmonicSelector
        ? (44 + 6 + 158 + 4 + 20 + 6 + 24 + 8 + 84 + 20 + 24 + 4 + 18)
        : (44 + 6 + 158 + 4 + 20 + 4 + 84 + 20 + 24 + 4 + 18);

    const auto topInset = juce::jmax (0, (area.getHeight() - contentHeight) / 2);
    area.removeFromTop (topInset);

    const auto bypassSize = 30;
    const auto bypassPad = 10;
    auto topRow = area.removeFromTop (44);
    bypassButton.setBounds (getWidth() - bypassPad - bypassSize, bypassPad, bypassSize, bypassSize);
    title.setBounds (topRow.reduced (24, 2).withTrimmedRight (bypassSize + bypassPad + 8));
    area.removeFromTop (6);

    amountKnob.setBounds (area.removeFromTop (158).withSizeKeepingCentre (158, 158));
    area.removeFromTop (4);

    amountLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    if (showHarmonicSelector)
    {
        harmonicType.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
    }
    else
    {
        harmonicType.setBounds ({});
        area.removeFromTop (4);
    }

    wetDryKnob.setBounds (area.removeFromTop (84).withSizeKeepingCentre (84, 84));
    wetDryLabel.setBounds (wetDryKnob.getX(), wetDryKnob.getBottom() + 1, wetDryKnob.getWidth(), 20);

    area.removeFromTop (20);

    frequencyRangeSlider.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto labels = area.removeFromTop (20);
    frequencyALabel.setBounds (labels.removeFromLeft (labels.getWidth() / 2));
    frequencyBLabel.setBounds (labels);
}

void CognitoniBlkFxAudioProcessorEditor::configureAmountKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
    slider.setRange (0.0, 1.0, 0.001);
}

void CognitoniBlkFxAudioProcessorEditor::configureRangeSlider (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::TwoValueHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0, 0.001);
}

CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::CognitoniLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB (81, 166, 214));
    setColour (juce::Slider::thumbColourId, juce::Colour::fromRGB (81, 166, 214));
    setColour (juce::Slider::trackColourId, juce::Colour::fromRGB (41, 51, 61));
    setColour (juce::Slider::textBoxTextColourId, juce::Colour::fromRGB (40, 46, 52));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB (187, 193, 199));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB (228, 232, 236));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour::fromRGB (35, 47, 56));
    setColour (juce::ComboBox::textColourId, juce::Colour::fromRGB (235, 240, 244));
    setColour (juce::ComboBox::outlineColourId, juce::Colour::fromRGB (173, 181, 188));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour::fromRGB (230, 233, 236));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                                                  int x,
                                                                                  int y,
                                                                                  int width,
                                                                                  int height,
                                                                                  float sliderPosProportional,
                                                                                  float rotaryStartAngle,
                                                                                  float rotaryEndAngle,
                                                                                  juce::Slider& slider)
{
    juce::ignoreUnused (slider);

    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (6.0f);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = juce::jmap (sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

    g.setColour (juce::Colour::fromRGB (224, 228, 232));
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.drawEllipse (bounds, 2.0f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 6.0f, radius - 6.0f, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour::fromRGB (44, 56, 66));
    g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius - 6.0f, radius - 6.0f, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour (findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valueArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto markerRadius = radius - 6.0f;
    const auto marker = centre + juce::Point<float> (std::cos (angle - juce::MathConstants<float>::halfPi) * markerRadius,
                                                     std::sin (angle - juce::MathConstants<float>::halfPi) * markerRadius);
    g.setColour (juce::Colour::fromRGB (81, 166, 214));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (marker));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                                                                  int x,
                                                                                  int y,
                                                                                  int width,
                                                                                  int height,
                                                                                  float sliderPos,
                                                                                  float minSliderPos,
                                                                                  float maxSliderPos,
                                                                                  juce::Slider::SliderStyle style,
                                                                                  juce::Slider& slider)
{
    juce::ignoreUnused (sliderPos, slider);

    if (style != juce::Slider::TwoValueHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto centreY = static_cast<float> (y + (height / 2));
    const auto left = static_cast<float> (x + 8);
    const auto right = static_cast<float> (x + width - 8);

    g.setColour (juce::Colour::fromRGB (41, 51, 61));
    g.drawLine (left, centreY, right, centreY, 5.0f);

    g.setColour (juce::Colour::fromRGB (81, 166, 214));
    g.drawLine (minSliderPos, centreY, maxSliderPos, centreY, 5.0f);

    const auto thumbSize = 10.0f;
    g.fillEllipse (juce::Rectangle<float> (thumbSize, thumbSize).withCentre ({ minSliderPos, centreY }));
    g.fillEllipse (juce::Rectangle<float> (thumbSize, thumbSize).withCentre ({ maxSliderPos, centreY }));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawComboBox (juce::Graphics& g,
                                                                              int width,
                                                                              int height,
                                                                              bool isButtonDown,
                                                                              int buttonX,
                                                                              int buttonY,
                                                                              int buttonW,
                                                                              int buttonH,
                                                                              juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown);

    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    juce::Path arrow;
    const auto arrowX = static_cast<float> (buttonX + (buttonW / 2));
    const auto arrowY = static_cast<float> (buttonY + (buttonH / 2));
    arrow.startNewSubPath (arrowX - 5.0f, arrowY - 2.0f);
    arrow.lineTo (arrowX, arrowY + 3.0f);
    arrow.lineTo (arrowX + 5.0f, arrowY - 2.0f);

    g.setColour (juce::Colour::fromRGB (228, 232, 236));
    g.strokePath (arrow, juce::PathStrokeType (2.0f));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                                                  juce::ToggleButton& button,
                                                                                  bool shouldDrawButtonAsHighlighted,
                                                                                  bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour::fromRGB (32, 38, 44));
    g.fillEllipse (bounds);

    const auto bypassed = button.getToggleState();
    const auto active = ! bypassed;
    g.setColour (active ? juce::Colour::fromRGB (84, 222, 134) : juce::Colour::fromRGB (130, 136, 144));
    g.fillEllipse (bounds.reduced (4.0f));

    g.setColour (juce::Colours::white.withAlpha (active ? 0.95f : 0.4f));
    g.drawEllipse (bounds.reduced (1.0f), 1.0f);
}

juce::Label* CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    return label;
}

CognitoniBlkFxAudioProcessorEditor::CognitoniBlkFxAudioProcessorEditor (CognitoniBlkFxAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    presetLabel.setText ("Preset:", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setFont (juce::FontOptions().withHeight (22.0f).withStyle ("SemiBold"));
    presetLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (24, 28, 33));
    addAndMakeVisible (presetLabel);

    refreshPresetSelectorItems();
    presetSelector.onChange = [this]
    {
        const auto selected = presetSelector.getSelectedId();
        if (selected > 0)
        {
            audioProcessor.setCurrentProgram (selected - 1);
            lastAppliedPresetSelectorId = audioProcessor.getCurrentPresetIndex() + 1;
            presetSelector.setSelectedId (lastAppliedPresetSelectorId, juce::dontSendNotification);
            deletePresetButton.setEnabled (audioProcessor.isPresetUserDeletable (audioProcessor.getCurrentPresetIndex()));
        }
    };
    presetSelector.setTooltip ("Select a stock or user preset");
    addAndMakeVisible (presetSelector);

    savePresetButton.setTooltip ("Save current settings as a new user preset");
    savePresetButton.setSvgIcon (EmbeddedIcons::saveSvg);
    savePresetButton.onClick = [this]
    {
        showSavePresetDialog();
    };
    addAndMakeVisible (savePresetButton);

    deletePresetButton.setTooltip ("Delete selected user preset (stock presets cannot be deleted)");
    deletePresetButton.setSvgIcon (EmbeddedIcons::deleteSvg);
    deletePresetButton.onClick = [this]
    {
        showDeletePresetDialog();
    };
    addAndMakeVisible (deletePresetButton);

    versionLabel.setText ("v0.1.0alpha", juce::dontSendNotification);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    versionLabel.setFont (juce::FontOptions().withHeight (16.0f).withStyle ("Regular"));
    versionLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (82, 90, 99));
    addAndMakeVisible (versionLabel);

    debugInfoLabel.setJustificationType (juce::Justification::centredRight);
    debugInfoLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (66, 74, 82));
    addChildComponent (debugInfoLabel);
    debugInfoLabel.setVisible (false);

    addAndMakeVisible (autoHarmCard);
    addAndMakeVisible (contrastCard);
    addAndMakeVisible (sawsCard);
    autoHarmCard.setHarmonicSelectorVisible (false);
    contrastCard.setHarmonicSelectorVisible (false);
    sawsCard.setHarmonicSelectorVisible (false);

    addAndMakeVisible (masterWetDryKnob);
    addAndMakeVisible (masterWetDryLabel);

    autoHarmCard.title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (46, 52, 58));
    contrastCard.title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (46, 52, 58));
    sawsCard.title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (46, 52, 58));
    autoHarmCard.amountLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (60, 66, 73));
    contrastCard.amountLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (60, 66, 73));
    sawsCard.amountLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (60, 66, 73));
    autoHarmCard.amountLabel.setText ("dB", juce::dontSendNotification);
    contrastCard.amountLabel.setText ("dB", juce::dontSendNotification);
    sawsCard.amountLabel.setText ("dB", juce::dontSendNotification);
    autoHarmCard.wetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (28, 32, 36));
    contrastCard.wetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (28, 32, 36));
    sawsCard.wetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (28, 32, 36));
    autoHarmCard.frequencyALabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));
    autoHarmCard.frequencyBLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));
    contrastCard.frequencyALabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));
    contrastCard.frequencyBLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));
    sawsCard.frequencyALabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));
    sawsCard.frequencyBLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (88, 96, 104));

    configureAmountKnob (autoHarmCard.amountKnob);
    configureAmountKnob (contrastCard.amountKnob);
    configureAmountKnob (sawsCard.amountKnob);
    configureAmountKnob (autoHarmCard.wetDryKnob);
    configureAmountKnob (contrastCard.wetDryKnob);
    configureAmountKnob (sawsCard.wetDryKnob);
    configureAmountKnob (masterWetDryKnob);
    masterWetDryKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    masterWetDryKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 20);

    configureRangeSlider (autoHarmCard.frequencyRangeSlider);
    configureRangeSlider (contrastCard.frequencyRangeSlider);
    configureRangeSlider (sawsCard.frequencyRangeSlider);

    masterWetDryLabel.setText ("Mix", juce::dontSendNotification);
    masterWetDryLabel.setJustificationType (juce::Justification::centredLeft);
    masterWetDryLabel.setFont (juce::FontOptions().withHeight (16.0f).withStyle ("SemiBold"));
    masterWetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (28, 32, 36));

    autoHarmAmountAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramAutoHarmWetDry, autoHarmCard.amountKnob);
    autoHarmTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.getAPVTS(), paramAutoHarmType, autoHarmCard.harmonicType);
    autoHarmWetDryAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramAutoHarmIntensity, autoHarmCard.wetDryKnob);
    autoHarmBypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.getAPVTS(), paramAutoHarmBypass, autoHarmCard.bypassButton);

    contrastAmountAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramContrastWetDry, contrastCard.amountKnob);
    contrastTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.getAPVTS(), paramContrastType, contrastCard.harmonicType);
    contrastWetDryAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramContrastAmount, contrastCard.wetDryKnob);
    contrastBypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.getAPVTS(), paramContrastBypass, contrastCard.bypassButton);

    sawsAmountAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramSawsWetDry, sawsCard.amountKnob);
    sawsTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.getAPVTS(), paramSawsType, sawsCard.harmonicType);
    sawsWetDryAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramSawsAmount, sawsCard.wetDryKnob);
    sawsBypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.getAPVTS(), paramSawsBypass, sawsCard.bypassButton);

    masterWetDryAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramMasterWetDry, masterWetDryKnob);

    autoHarmMinFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramAutoHarmMinFreq);
    autoHarmMaxFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramAutoHarmMaxFreq);
    contrastMinFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramContrastMinFreq);
    contrastMaxFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramContrastMaxFreq);
    sawsMinFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramSawsMinFreq);
    sawsMaxFreqParam = audioProcessor.getAPVTS().getRawParameterValue (paramSawsMaxFreq);

    autoHarmCard.frequencyRangeSlider.onValueChange = [this] { pushRangeSliderToParams(); };
    contrastCard.frequencyRangeSlider.onValueChange = [this] { pushRangeSliderToParams(); };
    sawsCard.frequencyRangeSlider.onValueChange = [this] { pushRangeSliderToParams(); };
    autoHarmCard.frequencyRangeSlider.textFromValueFunction = [this] (double value) { return normalisedToHzText (value); };
    contrastCard.frequencyRangeSlider.textFromValueFunction = [this] (double value) { return normalisedToHzText (value); };
    sawsCard.frequencyRangeSlider.textFromValueFunction = [this] (double value) { return normalisedToHzText (value); };

    autoHarmCard.amountKnob.textFromValueFunction = [] (double value) { return normalisedToDbText (value); };
    contrastCard.amountKnob.textFromValueFunction = [] (double value) { return normalisedToDbText (value); };
    sawsCard.amountKnob.textFromValueFunction = [] (double value) { return normalisedToDbText (value); };

    // valueFromTextFunction so that typing e.g. "-6" or "-6 dB" into the dB knob text box is parsed correctly.
    // Normalised value = (dB + 60) / 100, matching lin_interp(param, -60, +40) in the original.
    auto dbFromText = [] (const juce::String& text) -> double
    {
        auto t = text.trim().toLowerCase();
        if (t.startsWith ("-inf"))
            return 0.0;
        auto numStr = t.upToFirstOccurrenceOf ("db", false, true).trim();
        if (numStr.isEmpty())
            numStr = t;
        const auto db = numStr.getDoubleValue();
        return juce::jlimit (0.0, 1.0, (db + 60.0) / 100.0);
    };
    autoHarmCard.amountKnob.valueFromTextFunction = dbFromText;
    contrastCard.amountKnob.valueFromTextFunction = dbFromText;
    sawsCard.amountKnob.valueFromTextFunction = dbFromText;

    auto wetText = [] (double value)
    {
        return juce::String (value * 100.0, 0) + "%";
    };

    autoHarmCard.wetDryKnob.textFromValueFunction = [] (double value) { return autoHarmValueText (value); };
    contrastCard.wetDryKnob.textFromValueFunction = [] (double value) { return contrastValueText (value); };
    sawsCard.wetDryKnob.textFromValueFunction = [] (double value) { return sawsValueText (value); };
    masterWetDryKnob.textFromValueFunction = wetText;

    // valueFromTextFunction so typing "50" or "50%" into the master mix text box works.
    masterWetDryKnob.valueFromTextFunction = [] (const juce::String& text) -> double
    {
        const auto pct = text.trim().replace ("%", "").trim().getDoubleValue();
        return juce::jlimit (0.0, 1.0, pct / 100.0);
    };

    autoHarmCard.amountKnob.setTooltip ("AutoHarm dB gain: -inf to +40 dB (-60=off, 0dB at 60%)");
    contrastCard.amountKnob.setTooltip ("Contrast dB gain: -inf to +40 dB (-60=off, 0dB at 60%)");
    sawsCard.amountKnob.setTooltip ("Saws dB gain: -inf to +40 dB (-60=off, 0dB at 60%)");
    autoHarmCard.harmonicType.setTooltip ("Select harmonic mode for AutoHarm");
    contrastCard.harmonicType.setTooltip ("Select harmonic mode for Contrast");
    sawsCard.harmonicType.setTooltip ("Select harmonic mode for Saws");
    autoHarmCard.wetDryKnob.setTooltip ("AutoHarm value (Both/Odd/Even/Between)");
    contrastCard.wetDryKnob.setTooltip ("Contrast value (-100% to +100%)");
    sawsCard.wetDryKnob.setTooltip ("Saws value mode: scale0-100 or copy0-100");
    autoHarmCard.bypassButton.setTooltip ("Bypass AutoHarm card");
    contrastCard.bypassButton.setTooltip ("Bypass Contrast card");
    sawsCard.bypassButton.setTooltip ("Bypass Saws card");
    autoHarmCard.frequencyRangeSlider.setTooltip ("Set AutoHarm frequency start and end range");
    contrastCard.frequencyRangeSlider.setTooltip ("Set Contrast frequency start and end range");
    sawsCard.frequencyRangeSlider.setTooltip ("Set Saws frequency start and end range");
    masterWetDryKnob.setTooltip ("Global wet/dry mix for plugin output");

    deletePresetButton.setEnabled (audioProcessor.isPresetUserDeletable (audioProcessor.getCurrentPresetIndex()));

    autoHarmCard.amountKnob.setValue (autoHarmCard.amountKnob.getValue(), juce::dontSendNotification);
    contrastCard.amountKnob.setValue (contrastCard.amountKnob.getValue(), juce::dontSendNotification);
    sawsCard.amountKnob.setValue (sawsCard.amountKnob.getValue(), juce::dontSendNotification);
    autoHarmCard.wetDryKnob.setValue (autoHarmCard.wetDryKnob.getValue(), juce::dontSendNotification);
    contrastCard.wetDryKnob.setValue (contrastCard.wetDryKnob.getValue(), juce::dontSendNotification);
    sawsCard.wetDryKnob.setValue (sawsCard.wetDryKnob.getValue(), juce::dontSendNotification);
    masterWetDryKnob.setValue (masterWetDryKnob.getValue(), juce::dontSendNotification);
    autoHarmCard.amountKnob.updateText();
    contrastCard.amountKnob.updateText();
    sawsCard.amountKnob.updateText();
    autoHarmCard.wetDryKnob.updateText();
    contrastCard.wetDryKnob.updateText();
    sawsCard.wetDryKnob.updateText();
    masterWetDryKnob.updateText();

    setResizable (true, true);
    setResizeLimits (1260, 700, 1900, 1100);
    setSize (1380, 760);
    syncRangeSlidersFromParams();
    refreshFrequencyLabels();
    startTimerHz (20);
}

CognitoniBlkFxAudioProcessorEditor::~CognitoniBlkFxAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void CognitoniBlkFxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (208, 212, 217));

    auto panel = getLocalBounds().toFloat().reduced (16.0f);

    g.setColour (juce::Colour::fromRGBA (112, 118, 126, 58));
    g.fillRoundedRectangle (panel.translated (0.0f, 8.0f), 22.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (238, 241, 244), panel.getTopLeft(),
                                             juce::Colour::fromRGB (224, 228, 232), panel.getBottomLeft(), false));
    g.fillRoundedRectangle (panel, 22.0f);

    g.setColour (juce::Colour::fromRGB (249, 250, 252).withAlpha (0.85f));
    g.drawRoundedRectangle (panel.reduced (1.25f), 22.0f, 2.0f);
}

void CognitoniBlkFxAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);

    auto footer = area.removeFromBottom (22);
    versionLabel.setBounds (footer.removeFromRight (130));
    debugInfoLabel.setBounds (footer.removeFromLeft (juce::jmin (380, footer.getWidth())));

    auto topBand = area.removeFromTop (84);
    auto centerArea = area.reduced (0, 6);

    const auto cardGap = 18;
    const auto maxCardWidth = 350;
    const auto minCardWidth = 250;
    const auto availableForCards = centerArea.getWidth() - (cardGap * 2);
    const auto cardWidth = juce::jmin (maxCardWidth, juce::jmax (minCardWidth, availableForCards / 3));
    const auto cardsTotalWidth = (cardWidth * 3) + (cardGap * 2);
    const auto cardHeight = juce::jmin (470, juce::jmax (420, centerArea.getHeight() - 6));
    auto cardsRow = centerArea.withSizeKeepingCentre (cardsTotalWidth, cardHeight);

    const auto leftCardBounds = cardsRow.removeFromLeft (cardWidth);
    cardsRow.removeFromLeft (cardGap);
    const auto centerCardBounds = cardsRow.removeFromLeft (cardWidth);
    cardsRow.removeFromLeft (cardGap);
    const auto rightCardBounds = cardsRow.removeFromLeft (cardWidth);

    autoHarmCard.setBounds (leftCardBounds);
    contrastCard.setBounds (centerCardBounds);
    sawsCard.setBounds (rightCardBounds);

    auto leftSection = topBand.withX (leftCardBounds.getX()).withWidth ((centerCardBounds.getRight() - leftCardBounds.getX()) - 36);
    const auto iconButtonW = 30;
    const auto rowGap = 6;

    const auto desiredBoxWidth = 180; 

    auto selectorRow = leftSection.removeFromBottom (34);

    auto selectorBounds = selectorRow.removeFromLeft (desiredBoxWidth);
    presetSelector.setBounds (selectorBounds.withSizeKeepingCentre (desiredBoxWidth, 28));

    selectorRow.removeFromLeft (rowGap);
    savePresetButton.setBounds (selectorRow.removeFromLeft (iconButtonW).withSizeKeepingCentre (iconButtonW, iconButtonW));

    selectorRow.removeFromLeft (rowGap);
    deletePresetButton.setBounds (selectorRow.removeFromLeft (iconButtonW).withSizeKeepingCentre (iconButtonW, iconButtonW));

    presetLabel.setBounds (selectorBounds.withY (selectorBounds.getY() - 22).withHeight (20));

    // Master Mix — horizontal linear slider at right side of the preset row.
    const auto mixSliderW = 200;
    const auto mixLabelW  = 36;
    const auto mixGap     = 6;
    const auto mixTotalW  = mixLabelW + mixGap + mixSliderW;
    const auto mixRowH    = 26;
    const auto mixRowX    = rightCardBounds.getRight() - mixTotalW;
    const auto mixRowY    = topBand.getBottom() - mixRowH;
    masterWetDryLabel.setBounds (mixRowX, mixRowY, mixLabelW, mixRowH);
    masterWetDryKnob.setBounds  (mixRowX + mixLabelW + mixGap, mixRowY - 1, mixSliderW, mixRowH + 2);
}

void CognitoniBlkFxAudioProcessorEditor::refreshPresetSelectorItems()
{
    presetSelector.clear();
    const auto presetNames = audioProcessor.getPresetNames();
    for (int i = 0; i < presetNames.size(); ++i)
        presetSelector.addItem (presetNames[i], i + 1);

    lastAppliedPresetSelectorId = audioProcessor.getCurrentPresetIndex() + 1;
    presetSelector.setSelectedId (lastAppliedPresetSelectorId, juce::dontSendNotification);
    deletePresetButton.setEnabled (audioProcessor.isPresetUserDeletable (audioProcessor.getCurrentPresetIndex()));
}

void CognitoniBlkFxAudioProcessorEditor::showSavePresetDialog()
{
    auto* dialog = new juce::AlertWindow ("Save Preset",
                                          "Enter a preset name:",
                                          juce::AlertWindow::NoIcon);

    dialog->addTextEditor ("presetName", "", "Name");
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));

    dialog->enterModalState (true,
                             juce::ModalCallbackFunction::create ([this, dialog] (int result)
                             {
                                 if (result != 1)
                                     return;

                                 const auto presetName = dialog->getTextEditorContents ("presetName").trim();
                                 if (presetName.isEmpty())
                                     return;

                                 if (! audioProcessor.saveCurrentPresetAs (presetName))
                                     return;

                                 refreshPresetSelectorItems();
                             }),
                             true);
}

void CognitoniBlkFxAudioProcessorEditor::showDeletePresetDialog()
{
    const auto index = audioProcessor.getCurrentPresetIndex();
    if (! audioProcessor.isPresetUserDeletable (index))
        return;

    auto* confirm = new juce::AlertWindow ("Delete Preset",
                                           "Delete selected preset permanently?",
                                           juce::AlertWindow::WarningIcon);

    confirm->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    confirm->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));

    confirm->enterModalState (true,
                              juce::ModalCallbackFunction::create ([this] (int result)
                              {
                                  if (result != 1)
                                      return;

                                  const auto selectedIndex = audioProcessor.getCurrentPresetIndex();
                                  if (! audioProcessor.deletePresetByIndex (selectedIndex))
                                      return;

                                  refreshPresetSelectorItems();
                              }),
                              true);
}

juce::String CognitoniBlkFxAudioProcessorEditor::normalisedToHzText (double normalisedValue) const
{
    const auto hz = normalisedToDtBlkHzUi (normalisedValue, static_cast<double> (audioProcessor.getCurrentNyquistHz()));
    return juce::String (hz < 1000.0 ? hz : hz / 1000.0, 1) + (hz < 1000.0 ? "Hz" : "kHz");
}

juce::String CognitoniBlkFxAudioProcessorEditor::normalisedToPercentText (double normalisedValue)
{
    const auto pct = juce::jlimit (0.0, 1.0, normalisedValue) * 100.0;
    return juce::String (pct, 0) + "%";
}

void CognitoniBlkFxAudioProcessorEditor::syncRangeSlidersFromParams()
{
    if (autoHarmMinFreqParam == nullptr || autoHarmMaxFreqParam == nullptr
        || contrastMinFreqParam == nullptr || contrastMaxFreqParam == nullptr
        || sawsMinFreqParam == nullptr || sawsMaxFreqParam == nullptr)
    {
        return;
    }

    const auto autoA = juce::jlimit (0.0, 1.0, static_cast<double> (autoHarmMinFreqParam->load (std::memory_order_relaxed)));
    const auto autoB = juce::jlimit (0.0, 1.0, static_cast<double> (autoHarmMaxFreqParam->load (std::memory_order_relaxed)));

    const auto contrastA = juce::jlimit (0.0, 1.0, static_cast<double> (contrastMinFreqParam->load (std::memory_order_relaxed)));
    const auto contrastB = juce::jlimit (0.0, 1.0, static_cast<double> (contrastMaxFreqParam->load (std::memory_order_relaxed)));
    const auto sawsA = juce::jlimit (0.0, 1.0, static_cast<double> (sawsMinFreqParam->load (std::memory_order_relaxed)));
    const auto sawsB = juce::jlimit (0.0, 1.0, static_cast<double> (sawsMaxFreqParam->load (std::memory_order_relaxed)));

    updatingRangeControls = true;
    autoHarmCard.frequencyRangeSlider.setMinValue (autoA, juce::dontSendNotification, false);
    autoHarmCard.frequencyRangeSlider.setMaxValue (autoB, juce::dontSendNotification, false);
    contrastCard.frequencyRangeSlider.setMinValue (contrastA, juce::dontSendNotification, false);
    contrastCard.frequencyRangeSlider.setMaxValue (contrastB, juce::dontSendNotification, false);
    sawsCard.frequencyRangeSlider.setMinValue (sawsA, juce::dontSendNotification, false);
    sawsCard.frequencyRangeSlider.setMaxValue (sawsB, juce::dontSendNotification, false);
    updatingRangeControls = false;
}

void CognitoniBlkFxAudioProcessorEditor::setParameterNormalised (const juce::String& parameterId, float value)
{
    if (auto* parameter = audioProcessor.getAPVTS().getParameter (parameterId))
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
}

void CognitoniBlkFxAudioProcessorEditor::pushRangeSliderToParams()
{
    if (updatingRangeControls)
        return;

    setParameterNormalised (paramAutoHarmMinFreq, static_cast<float> (autoHarmCard.frequencyRangeSlider.getMinValue()));
    setParameterNormalised (paramAutoHarmMaxFreq, static_cast<float> (autoHarmCard.frequencyRangeSlider.getMaxValue()));

    setParameterNormalised (paramContrastMinFreq, static_cast<float> (contrastCard.frequencyRangeSlider.getMinValue()));
    setParameterNormalised (paramContrastMaxFreq, static_cast<float> (contrastCard.frequencyRangeSlider.getMaxValue()));

    setParameterNormalised (paramSawsMinFreq, static_cast<float> (sawsCard.frequencyRangeSlider.getMinValue()));
    setParameterNormalised (paramSawsMaxFreq, static_cast<float> (sawsCard.frequencyRangeSlider.getMaxValue()));
}

void CognitoniBlkFxAudioProcessorEditor::timerCallback()
{
    syncRangeSlidersFromParams();
    refreshFrequencyLabels();

    autoHarmCard.amountKnob.updateText();
    contrastCard.amountKnob.updateText();
    sawsCard.amountKnob.updateText();
    autoHarmCard.wetDryKnob.updateText();
    contrastCard.wetDryKnob.updateText();
    sawsCard.wetDryKnob.updateText();
    masterWetDryKnob.updateText();

    if (! presetSelector.isPopupActive())
    {
        const auto selectedId = presetSelector.getSelectedId();
        if (selectedId > 0 && selectedId != lastAppliedPresetSelectorId)
        {
            audioProcessor.setCurrentProgram (selectedId - 1);
            lastAppliedPresetSelectorId = audioProcessor.getCurrentPresetIndex() + 1;
            presetSelector.setSelectedId (lastAppliedPresetSelectorId, juce::dontSendNotification);
        }

        const auto currentPresetId = audioProcessor.getCurrentPresetIndex() + 1;
        if (presetSelector.getSelectedId() != currentPresetId)
        {
            presetSelector.setSelectedId (currentPresetId, juce::dontSendNotification);
            lastAppliedPresetSelectorId = currentPresetId;
        }
    }

    deletePresetButton.setEnabled (audioProcessor.isPresetUserDeletable (audioProcessor.getCurrentPresetIndex()));

    debugInfoLabel.setText (
        "SR " + juce::String (audioProcessor.getSampleRate(), 1)
            + " Hz | InCh " + juce::String (audioProcessor.getLastInputChannels())
            + " OutCh " + juce::String (audioProcessor.getLastOutputChannels())
            + " | In RMS " + juce::String (audioProcessor.getLastInputRms(), 5)
            + " | Out RMS " + juce::String (audioProcessor.getLastOutputRms(), 5)
            + " | Sanitized " + juce::String (audioProcessor.getLastSanitisedSamples()),
        juce::dontSendNotification);
}

void CognitoniBlkFxAudioProcessorEditor::refreshFrequencyLabels()
{
    autoHarmCard.frequencyALabel.setText ("Start " + normalisedToHzText (autoHarmCard.frequencyRangeSlider.getMinValue()), juce::dontSendNotification);
    autoHarmCard.frequencyBLabel.setText ("End " + normalisedToHzText (autoHarmCard.frequencyRangeSlider.getMaxValue()), juce::dontSendNotification);

    contrastCard.frequencyALabel.setText ("Start " + normalisedToHzText (contrastCard.frequencyRangeSlider.getMinValue()), juce::dontSendNotification);
    contrastCard.frequencyBLabel.setText ("End " + normalisedToHzText (contrastCard.frequencyRangeSlider.getMaxValue()), juce::dontSendNotification);

    sawsCard.frequencyALabel.setText ("Start " + normalisedToHzText (sawsCard.frequencyRangeSlider.getMinValue()), juce::dontSendNotification);
    sawsCard.frequencyBLabel.setText ("End " + normalisedToHzText (sawsCard.frequencyRangeSlider.getMaxValue()), juce::dontSendNotification);
}
