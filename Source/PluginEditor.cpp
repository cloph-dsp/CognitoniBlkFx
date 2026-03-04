/*
  ==============================================================================
    CognitoniBlkFx -- PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Icons/EmbeddedIcons.h"

#include <cmath>
#include <random>

namespace
{
constexpr auto paramMasterWetDry = "masterWetDry";
constexpr auto paramBlackLens    = "blackLens";
constexpr auto paramInputGain    = "inputGain";
constexpr auto paramOutputGain   = "outputGain";

constexpr double dtBlkC0Hz    = 16.3516;
constexpr double dtBlkNoteSpan = 255.0 * 0.5;

double normalisedToDtBlkHzUi (double normalised, double nyquistHz)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v <= 0.0) return 0.0;
    const auto hz = dtBlkC0Hz * std::pow (2.0, v * dtBlkNoteSpan / 12.0);
    return juce::jlimit (0.0, juce::jmax (20.0, nyquistHz), hz);
}

juce::String normalisedToDbText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v <= 0.0) return "-inf dB";
    return juce::String (v * 100.0 - 60.0, 1) + " dB";
}

juce::String autoHarmValueText (double normalised)
{
    const auto v      = juce::jlimit (0.0, 1.0, normalised);
    const auto scaled = v * 4.0;
    const auto part   = juce::jlimit (0, 3, static_cast<int> (std::floor (scaled)));
    const auto frac   = juce::jlimit (0.0, 0.999, scaled - static_cast<double> (part));
    const auto pct    = static_cast<int> (std::round (frac * 100.0));
    switch (part)
    {
        case 0:  return juce::String (pct) + "% Both";
        case 1:  return juce::String (pct) + "% Odd";
        case 2:  return juce::String (pct) + "% Even";
        default: return juce::String (pct) + "% Between";
    }
}

// Bipolar -100%..100% display for Contrast card value
juce::String contrastValueText (double normalised)
{
    const auto v   = juce::jlimit (0.0, 1.0, normalised);
    const int  pct = juce::roundToInt (v * 200.0 - 100.0);
    return juce::String (pct) + "%";
}

// "0% Scale" → "100% Scale" / "0% Copy" → "100% Copy" for Saws value
juce::String sawsValueText (double normalised)
{
    const auto v = juce::jlimit (0.0, 1.0, normalised);
    if (v < 0.5)
        return juce::String (juce::roundToInt (v * 200.0)) + "% Scale";
    return juce::String (juce::roundToInt ((v - 0.5) * 200.0)) + "% Copy";
}
} // namespace

//==============================================================================
//  CardComponent
//==============================================================================
CognitoniBlkFxAudioProcessorEditor::CardComponent::CardComponent()
{
    addAndMakeVisible (title);
    title.setJustificationType (juce::Justification::centredLeft);
    title.setFont (juce::FontOptions().withName ("Montserrat").withHeight (12.5f).withStyle ("SemiBold"));
    title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (38, 36, 33));

    addAndMakeVisible (bypassButton);
    bypassButton.setButtonText (" ");
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);

    addAndMakeVisible (removeButton);
    removeButton.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97"));  // UTF-8 multiplication sign
    removeButton.setVisible (false);

    addAndMakeVisible (amountKnob);
    amountKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (amountHeaderLabel);
    amountHeaderLabel.setText ("dB", juce::dontSendNotification);
    amountHeaderLabel.setJustificationType (juce::Justification::centred);
    amountHeaderLabel.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.5f));
    amountHeaderLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (120, 115, 110));

    addAndMakeVisible (harmonicType);
    // Harmonic type selector: rotary knob (0=Odd, 1=Even, 2=Both, 3=Between)
    harmonicType.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    harmonicType.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    harmonicType.setRange (0.0, 3.0, 1.0);
    harmonicType.textFromValueFunction = [] (double v) -> juce::String
    {
        const int i = juce::roundToInt (juce::jlimit (0.0, 3.0, v));
        const char* names[] = { "Odd", "Even", "Both", "Between" };
        return names[i];
    };
    addAndMakeVisible (harmonicTypeLabel);
    harmonicTypeLabel.setText ("Type", juce::dontSendNotification);
    harmonicTypeLabel.setJustificationType (juce::Justification::centred);
    harmonicTypeLabel.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.5f));
    harmonicTypeLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (120, 115, 110));

    addAndMakeVisible (wetDryKnob);
    wetDryKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (wetDryHeaderLabel);
    wetDryHeaderLabel.setText ("Value", juce::dontSendNotification);
    wetDryHeaderLabel.setJustificationType (juce::Justification::centred);
    wetDryHeaderLabel.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.5f));
    wetDryHeaderLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (120, 115, 110));

    addAndMakeVisible (frequencyRangeSlider);
    frequencyRangeSlider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    frequencyRangeSlider.setColour (juce::Slider::thumbColourId, accent);

    addAndMakeVisible (freqStartHeader);
    addAndMakeVisible (freqEndHeader);
    freqStartHeader.setText ("Start", juce::dontSendNotification);
    freqEndHeader  .setText ("End",   juce::dontSendNotification);
    freqStartHeader.setJustificationType (juce::Justification::centredLeft);
    freqEndHeader  .setJustificationType (juce::Justification::centredRight);
    freqStartHeader.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.0f));
    freqEndHeader  .setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.0f));
    freqStartHeader.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));
    freqEndHeader  .setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));

    addAndMakeVisible (frequencyALabel);
    addAndMakeVisible (frequencyBLabel);
    frequencyALabel.setJustificationType (juce::Justification::centredLeft);
    frequencyBLabel.setJustificationType (juce::Justification::centredRight);
    frequencyALabel.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.0f));
    frequencyBLabel.setFont (juce::FontOptions().withName ("Montserrat").withHeight (11.0f));
    frequencyALabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));
    frequencyBLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));

    setMouseCursor (juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus (false);

    // Default: show empty state
    showFilledControls (false);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::showFilledControls (bool show)
{
    title.setVisible       (show);
    bypassButton.setVisible(show);
    removeButton.setVisible(show);
    amountKnob.setVisible  (show);
    amountHeaderLabel.setVisible (show);
    harmonicType.setVisible(false);
    harmonicTypeLabel.setVisible(false);
    wetDryKnob.setVisible  (show);
    wetDryHeaderLabel.setVisible (show);
    frequencyRangeSlider.setVisible (show);
    freqStartHeader.setVisible (show);
    freqEndHeader.setVisible   (show);
    frequencyALabel.setVisible (show);
    frequencyBLabel.setVisible (show);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::setCardType (CardSchema::CardType newType,
                                                                      const juce::String& titleText)
{
    currentCardType = newType;
    accent = CardSchema::accentColour (newType);

    amountKnob.setColour  (juce::Slider::rotarySliderFillColourId, accent);
    wetDryKnob.setColour  (juce::Slider::rotarySliderFillColourId, accent);
    harmonicType.setColour (juce::Slider::rotarySliderFillColourId, accent);
    frequencyRangeSlider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    frequencyRangeSlider.setColour (juce::Slider::thumbColourId, accent);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);

    if (newType == CardSchema::CardType::empty)
    {
        showFilledControls (false);
    }
    else
    {
        showFilledControls (true);
        const auto name = titleText.isNotEmpty() ? titleText
                                                  : juce::String (CardSchema::cardTypeName (newType));
        title.setText (name.toUpperCase(), juce::dontSendNotification);
        wetDryHeaderLabel.setText (CardSchema::wetDryKnobLabel (newType), juce::dontSendNotification);
        const bool hasHarm = CardSchema::hasHarmonicSelector (newType);
        harmonicType.setVisible (hasHarm);
        harmonicTypeLabel.setVisible (hasHarm);
    }

    resized();
    repaint();
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::paint (juce::Graphics& g)
{
    const float sf = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);
    auto bounds = getLocalBounds().toFloat();
    const float r = 12.0f;

    if (currentCardType == CardSchema::CardType::empty)
    {
        // Hover background tint
        if (isHovered)
        {
            g.setColour (juce::Colour::fromRGBA (185, 180, 172, 22));
            g.fillRoundedRectangle (bounds, r);
        }
        // Dashed border
        g.setColour (isHovered ? juce::Colour::fromRGB (170, 165, 158)
                               : juce::Colour::fromRGB (220, 215, 208));
        g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.5f);
        const float dash[] = { 6.0f, 4.0f };
        juce::Path dashed;
        dashed.addRoundedRectangle (bounds.reduced (4.0f), r - 2.0f);
        g.setColour (isHovered ? juce::Colour::fromRGB (160, 155, 148)
                               : juce::Colour::fromRGB (190, 185, 178));
        juce::PathStrokeType pst (1.0f);
        juce::Path dashResult;
        pst.createDashedStroke (dashResult, dashed, dash, 2);
        g.strokePath (dashResult, pst);

        // Large centered "+" sign
        const float plusSize = juce::jmin ((float)getWidth(), (float)getHeight()) * 0.28f;
        g.setColour (isHovered ? juce::Colour::fromRGB (145, 140, 133)
                               : juce::Colour::fromRGB (195, 190, 184));
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (plusSize).withStyle ("Light")));
        g.drawText ("+", bounds, juce::Justification::centred, false);

        // Hover text hint
        if (isHovered)
        {
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.5f)));
            g.setColour (juce::Colour::fromRGB (130, 126, 120));
            g.drawText ("Click to add", bounds.withTrimmedTop (bounds.getHeight() * 0.62f),
                        juce::Justification::centred, false);
        }
        return;
    }

    // Card body
    g.setColour (juce::Colour::fromRGB (251, 249, 246));
    g.fillRoundedRectangle (bounds, r);
    // Highlight border when drag is over this card
    if (isDragTarget)
    {
        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (1.5f), r, 2.5f);
        // Extra inner glow to show card will land here
        g.setColour (accent.withAlpha (0.10f));
        g.fillRoundedRectangle (bounds, r);
    }
    else
    {
        g.setColour (juce::Colour::fromRGB (215, 210, 204));
        g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);
    }

    // Dim overlay while this card is being dragged (source card feedback)
    if (isDragSource)
    {
        g.setColour (juce::Colour::fromRGBA (220, 215, 208, 160));
        g.fillRoundedRectangle (bounds, r);
        // Dashed border to show "slot will be swapped"
        const float dash[] = { 6.0f, 4.0f };
        juce::Path dashed;
        dashed.addRoundedRectangle (bounds.reduced (3.0f), r - 1.0f);
        g.setColour (juce::Colour::fromRGB (175, 170, 162));
        juce::PathStrokeType pst (1.5f);
        juce::Path dashResult;
        pst.createDashedStroke (dashResult, dashed, dash, 2);
        g.strokePath (dashResult, pst);
    }

    // Separator line below title
    const float titleRowH = std::round (30.0f * sf);
    const float titleRowY = std::round (6.0f * sf);
    const float sepY      = bounds.getY() + titleRowY + titleRowH + 1.0f;
    const float sepInset  = std::round (14.0f * sf);
    g.setColour (juce::Colour::fromRGB (225, 220, 214));
    g.drawLine (bounds.getX() + sepInset, sepY, bounds.getRight() - sepInset, sepY, 1.0f);

    // Drag grip indicator (2 cols × 3 rows of dots) just right of bypass button
    {
        const float gPadX  = std::round (13.0f * sf);
        const float gBypW  = std::round (20.0f * sf);
        const float gripX  = bounds.getX() + gPadX + gBypW + std::round (6.0f * sf);
        const float gripCY = bounds.getY() + titleRowY + titleRowH * 0.5f;
        g.setColour (juce::Colour::fromRGBA (140, 134, 126, 100));
        const float dot   = 1.8f;
        const float dxSep = 4.0f;
        const float dySep = 4.0f;
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 3; ++row)
                g.fillEllipse (gripX + col * dxSep,
                               gripCY + (row - 1) * dySep - dot * 0.5f,
                               dot, dot);
    }

    // Icon area background
    const float iconY    = std::round (30.0f * sf) + std::round (6.0f * sf);
    const float iconH    = std::round (72.0f * sf);   // matches resized()
    const float iconPadX = std::round (16.0f * sf);
    auto iconRect = juce::Rectangle<float> (bounds.getX() + iconPadX, bounds.getY() + iconY,
                                            bounds.getWidth() - iconPadX * 2.0f, iconH);
    g.setColour (accent.withAlpha (0.06f));
    g.fillRoundedRectangle (iconRect, 8.0f);
    g.setColour (accent.withAlpha (0.12f));
    const float dotSpacing = juce::jmax (8.0f, 10.0f * sf);
    for (float dx = iconRect.getX() + dotSpacing; dx < iconRect.getRight() - 2.0f; dx += dotSpacing)
        for (float dy = iconRect.getY() + dotSpacing * 0.6f; dy < iconRect.getBottom() - 2.0f; dy += dotSpacing)
            g.fillEllipse (dx - 1.0f, dy - 1.0f, 2.0f, 2.0f);

    const float iconInset = juce::jmax (10.0f, 14.0f * sf);
    auto drawRect = iconRect.reduced (iconInset, iconInset * 0.6f);
    switch (currentCardType)
    {
        case CardSchema::CardType::autoHarm: drawAutoHarmIcon (g, drawRect, accent); break;
        case CardSchema::CardType::contrast: drawContrastIcon (g, drawRect, accent); break;
        case CardSchema::CardType::saws:     drawSawsIcon     (g, drawRect, accent); break;
        case CardSchema::CardType::smear:    drawSmearIcon    (g, drawRect, accent); break;
        default: break;
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::resized()
{
    const float sf   = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);
    const int W      = getWidth();
    const int H      = getHeight();
    const int padX   = juce::roundToInt (13.0f * sf);
    const int bypSz  = juce::roundToInt (20.0f * sf);
    const int cW     = W - 2 * padX;

    // Build font helpers using embedded Montserrat (or system fallback if not yet loaded)
    auto makeFont = [this] (float h) -> juce::Font
    {
        if (regularTypeface != nullptr)
            return juce::Font (juce::FontOptions().withTypeface (regularTypeface).withHeight (h));
        return juce::Font (juce::FontOptions().withName ("Segoe UI").withHeight (h));
    };
    auto makeBoldFont = [this] (float h) -> juce::Font
    {
        if (boldTypeface != nullptr)
            return juce::Font (juce::FontOptions().withTypeface (boldTypeface).withHeight (h));
        return juce::Font (juce::FontOptions().withName ("Segoe UI").withHeight (h).withStyle ("Bold"));
    };

    title.setFont          (makeFont (juce::jmax (10.0f, 13.0f * sf)));
    amountHeaderLabel.setFont  (makeFont (juce::jmax (10.0f, 11.5f * sf)));
    wetDryHeaderLabel.setFont  (makeFont (juce::jmax (10.0f, 11.5f * sf)));
    harmonicTypeLabel.setFont  (makeFont (juce::jmax (10.0f, 11.5f * sf)));
    freqStartHeader.setFont    (makeFont (juce::jmax  (9.0f, 11.0f * sf)));
    freqEndHeader.setFont      (makeFont (juce::jmax  (9.0f, 11.0f * sf)));
    frequencyALabel.setFont    (makeFont (juce::jmax  (9.0f, 11.0f * sf)));
    frequencyBLabel.setFont    (makeFont (juce::jmax  (9.0f, 11.0f * sf)));
    juce::ignoreUnused (makeBoldFont);

    if (currentCardType == CardSchema::CardType::empty)
    {
        // Empty card: whole area is the click target, nothing to lay out
        return;
    }

    // ── Title row: bypass left, title centred, remove right ────
    const int titleRowH = juce::roundToInt (30.0f * sf);
    const int titleRowY = juce::roundToInt (6.0f * sf);
    const int rmvSz     = juce::roundToInt (18.0f * sf);

    // Bypass on the LEFT
    bypassButton.setBounds (padX,
                            titleRowY + (titleRowH - bypSz) / 2,
                            bypSz, bypSz);
    // Remove on the RIGHT
    removeButton.setBounds (W - padX - rmvSz,
                            titleRowY + (titleRowH - rmvSz) / 2,
                            rmvSz, rmvSz);
    // Title centred in the remaining middle space
    const int titleX  = padX + bypSz + 4;
    const int titleW  = W - titleX - rmvSz - padX - 4;
    title.setBounds (titleX, titleRowY, titleW, titleRowH);
    title.setJustificationType (juce::Justification::centred);

    // ── Content area: distribute remaining height evenly ─────────────────
    const int iconH    = juce::roundToInt (72.0f * sf);  // must match paint()
    const int hdrH     = juce::roundToInt (13.0f * sf);
    const int bigKnob  = juce::roundToInt (72.0f * sf);
    const int smKnob   = juce::roundToInt (60.0f * sf);
    const int harmKnob = juce::roundToInt (52.0f * sf);
    const int sliderH  = juce::roundToInt (14.0f * sf);
    const int valLblH  = hdrH;
    const int kGap     = juce::roundToInt (10.0f * sf);   // uniform gap between sections

    // Start placing controls after separator + icon area
    // iconY in paint() = titleRowY + titleRowH = (6+30)*sf = 36*sf
    int y = juce::roundToInt ((6.0f + 30.0f) * sf) + iconH + kGap;

    // dB knob
    amountHeaderLabel.setBounds (padX, y, cW, hdrH);
    y += hdrH + 4;
    amountKnob.setBounds (padX + (cW - bigKnob) / 2, y, bigKnob, bigKnob);
    y += bigKnob + kGap;

    // Harmonic type knob (optional)
    const bool hasHarm = harmonicType.isVisible();
    if (hasHarm)
    {
        harmonicTypeLabel.setBounds (padX, y, cW, hdrH);
        y += hdrH + 4;
        harmonicType.setBounds (padX + (cW - harmKnob) / 2, y, harmKnob, harmKnob);
        y += harmKnob + kGap;
    }
    else
    {
        harmonicType.setBounds ({});
        harmonicTypeLabel.setBounds ({});
    }

    // Value/Smear knob
    wetDryHeaderLabel.setBounds (padX, y, cW, hdrH);
    y += hdrH + 4;
    wetDryKnob.setBounds (padX + (cW - smKnob) / 2, y, smKnob, smKnob);
    y += smKnob + kGap;

    // Freq range slider pinned toward bottom of card
    const int freqBlockH = hdrH + 4 + sliderH + 4 + valLblH;
    const int freqTop    = H - juce::roundToInt (10.0f * sf) - freqBlockH;
    const int freqY      = juce::jmax (y, freqTop);

    auto freqHdrRow = juce::Rectangle<int> (padX, freqY, cW, hdrH);
    freqStartHeader.setBounds (freqHdrRow.removeFromLeft (cW / 2));
    freqEndHeader  .setBounds (freqHdrRow);

    frequencyRangeSlider.setBounds (padX, freqY + hdrH + 4, cW, sliderH);

    auto labRow = juce::Rectangle<int> (padX, freqY + hdrH + 4 + sliderH + 4, cW, valLblH);
    frequencyALabel.setBounds (labRow.removeFromLeft (labRow.getWidth() / 2));
    frequencyBLabel.setBounds (labRow);

    repaint();
}

//  Icon drawing 
void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawAutoHarmIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    const float w = b.getWidth(), h = b.getHeight();
    auto pt = [&] (float nx, float ny) { return juce::Point<float> (b.getX() + nx * w, b.getY() + ny * h); };

    juce::Path back;
    back.startNewSubPath (pt (0.0f, 1.0f));
    back.lineTo (pt (0.10f, 0.70f)); back.lineTo (pt (0.22f, 0.52f));
    back.lineTo (pt (0.34f, 0.62f)); back.lineTo (pt (0.46f, 0.42f));
    back.lineTo (pt (0.58f, 0.48f)); back.lineTo (pt (0.70f, 0.38f));
    back.lineTo (pt (0.82f, 0.55f)); back.lineTo (pt (0.96f, 0.62f));
    back.lineTo (pt (1.0f,  1.0f));
    back.closeSubPath();
    g.setColour (col.withAlpha (0.12f));
    g.fillPath (back);
    g.setColour (col.withAlpha (0.28f));
    g.strokePath (back, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path front;
    front.startNewSubPath (pt (0.0f, 1.0f));
    front.lineTo (pt (0.08f, 0.60f)); front.lineTo (pt (0.16f, 0.40f));
    front.lineTo (pt (0.22f, 0.50f)); front.lineTo (pt (0.30f, 0.22f));
    front.lineTo (pt (0.36f, 0.07f)); front.lineTo (pt (0.42f, 0.18f));
    front.lineTo (pt (0.50f, 0.30f)); front.lineTo (pt (0.56f, 0.14f));
    front.lineTo (pt (0.64f, 0.28f)); front.lineTo (pt (0.70f, 0.42f));
    front.lineTo (pt (0.76f, 0.32f)); front.lineTo (pt (0.83f, 0.52f));
    front.lineTo (pt (0.90f, 0.46f)); front.lineTo (pt (0.96f, 0.60f));
    front.lineTo (pt (1.0f,  1.0f));
    front.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.30f), b.getTopLeft(),
                                             col.withAlpha (0.04f), b.getBottomLeft(), false));
    g.fillPath (front);
    g.setColour (col.withAlpha (0.90f));
    juce::Path frontStroke;
    frontStroke.startNewSubPath (pt (0.0f, 1.0f));
    frontStroke.lineTo (pt (0.08f, 0.60f)); frontStroke.lineTo (pt (0.16f, 0.40f));
    frontStroke.lineTo (pt (0.22f, 0.50f)); frontStroke.lineTo (pt (0.30f, 0.22f));
    frontStroke.lineTo (pt (0.36f, 0.07f)); frontStroke.lineTo (pt (0.42f, 0.18f));
    frontStroke.lineTo (pt (0.50f, 0.30f)); frontStroke.lineTo (pt (0.56f, 0.14f));
    frontStroke.lineTo (pt (0.64f, 0.28f)); frontStroke.lineTo (pt (0.70f, 0.42f));
    frontStroke.lineTo (pt (0.76f, 0.32f)); frontStroke.lineTo (pt (0.83f, 0.52f));
    frontStroke.lineTo (pt (0.90f, 0.46f)); frontStroke.lineTo (pt (0.96f, 0.60f));
    frontStroke.lineTo (pt (1.0f,  1.0f));
    g.strokePath (frontStroke, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (auto [nx, ny] : { std::pair{0.36f, 0.07f}, std::pair{0.56f, 0.14f} })
    {
        auto peak = pt (nx, ny);
        g.setColour (col);
        g.fillEllipse (peak.x - 3.0f, peak.y - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillEllipse (peak.x - 1.5f, peak.y - 1.5f, 3.0f, 3.0f);
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawContrastIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float rLong  = juce::jmin (b.getWidth(), b.getHeight()) * 0.46f;
    const float rShort = rLong * 0.40f;
    const int   nRays  = 8;
    juce::Path star;
    for (int i = 0; i < nRays * 2; ++i)
    {
        const float angle = juce::MathConstants<float>::twoPi * i / (nRays * 2)
                            - juce::MathConstants<float>::halfPi;
        const float r = (i % 2 == 0) ? rLong : rShort;
        const auto  p = juce::Point<float> (cx + std::cos (angle) * r, cy + std::sin (angle) * r);
        if (i == 0) star.startNewSubPath (p);
        else        star.lineTo (p);
    }
    star.closeSubPath();
    g.setColour (col.withAlpha (0.13f));
    g.fillPath (star);
    g.setColour (col.withAlpha (0.85f));
    g.strokePath (star, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    const float innerR = rShort * 0.65f;
    g.setColour (col.withAlpha (0.30f));
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.2f);
    g.setColour (col.withAlpha (0.18f));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (col);
    g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawSawsIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    const float w = b.getWidth(), h = b.getHeight();
    juce::ignoreUnused (h);
    auto pt = [&] (float nx, float ny) { return juce::Point<float> (b.getX() + nx * w, b.getY() + ny * (float)b.getHeight()); };

    const float baseY = 0.92f;
    g.setColour (col.withAlpha (0.25f));
    const float segW = 0.05f;
    for (float sx = 0.0f; sx < 1.0f; sx += segW * 2.0f)
        g.drawLine (juce::Line<float> (pt (sx, baseY), pt (juce::jmin (sx + segW, 1.0f), baseY)), 1.0f);

    auto drawSaw = [&] (float x0, float x1, int cycles)
    {
        const float cycleW = (x1 - x0) / cycles;
        juce::Path fill;
        fill.startNewSubPath (pt (x0, baseY));
        for (int c = 0; c < cycles; ++c)
        {
            const float xb = x0 + (c + 1) * cycleW;
            fill.lineTo (pt (xb - 0.001f, 0.08f));
            fill.lineTo (pt (xb - 0.001f, baseY));
        }
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.22f), b.getTopLeft(),
                                                  col.withAlpha (0.04f), b.getBottomLeft(), false));
        g.fillPath (fill);
        juce::Path saw;
        saw.startNewSubPath (pt (x0, baseY));
        for (int c = 0; c < cycles; ++c)
        {
            const float xb = x0 + (c + 1) * cycleW;
            saw.lineTo (pt (xb - 0.001f, 0.08f));
            saw.lineTo (pt (xb - 0.001f, baseY));
        }
        g.setColour (col.withAlpha (0.88f));
        g.strokePath (saw, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered,
                                                 juce::PathStrokeType::square));
    };
    drawSaw (0.02f, 0.44f, 2);
    drawSaw (0.56f, 0.98f, 2);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawSmearIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    // Three overlapping sine-wave-like blurred curves with decreasing opacity
    const float cx = b.getCentreX();
    const float w  = b.getWidth();
    const float h  = b.getHeight();

    const juce::PathStrokeType stroke (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    for (int pass = 0; pass < 3; ++pass)
    {
        const float phaseOffset = static_cast<float> (pass) * 0.42f;
        const float ampScale    = 1.0f - pass * 0.25f;
        const float alpha       = 0.9f - pass * 0.28f;

        juce::Path curve;
        bool first = true;
        for (int xi = 0; xi <= 40; ++xi)
        {
            const float t  = xi / 40.0f;
            const float x  = b.getX() + t * w;
            const float y  = b.getCentreY() + std::sin (t * juce::MathConstants<float>::twoPi * 1.8f + phaseOffset) * h * 0.36f * ampScale;
            juce::ignoreUnused (cx);
            if (first) { curve.startNewSubPath (x, y); first = false; }
            else        curve.lineTo (x, y);
        }
        g.setColour (col.withAlpha (alpha));
        g.strokePath (curve, stroke);
    }
}

//  CardComponent mouse events 
void CognitoniBlkFxAudioProcessorEditor::CardComponent::mouseEnter (const juce::MouseEvent&)
{
    if (currentCardType == CardSchema::CardType::empty)
    {
        isHovered = true;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::mouseExit (const juce::MouseEvent&)
{
    if (isHovered)
    {
        isHovered = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
    canStartDrag = false;
    dragStarted  = false;
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStarted  = false;
    // Allow drag only from the title row
    const float sf        = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);
    const int   titleYEnd = juce::roundToInt ((6.0f + 30.0f) * sf);
    canStartDrag  = (currentCardType != CardSchema::CardType::empty) && (e.y <= titleYEnd);
    dragClickPos  = e.getPosition();
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (canStartDrag && !dragStarted && e.getDistanceFromDragStart() > 8)
    {
        dragStarted  = true;
        canStartDrag = false;
        // Take a snapshot of the card as the drag image
        auto snapshot = juce::Component::createComponentSnapshot (getLocalBounds());
        juce::Image ghost (juce::Image::ARGB, snapshot.getWidth(), snapshot.getHeight(), true);
        {
            juce::Graphics gfx (ghost);
            gfx.setOpacity (0.62f);
            gfx.drawImageAt (snapshot, 0, 0);
        }
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            // Offset the ghost so the cursor stays at the same relative position as the click
            juce::Point<int> imgOffset (-dragClickPos.x, -dragClickPos.y);

            // Mark ourselves as the drag source (dimmed while dragging)
            isDragSource = true;
            repaint();

            container->startDragging ("card:" + juce::String (slotIndex), this,
                                      juce::ScaledImage (ghost), true, &imgOffset, nullptr);
        }
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::mouseUp (const juce::MouseEvent&)
{
    // Click anywhere on empty card to open card picker
    if (currentCardType == CardSchema::CardType::empty && !dragStarted && onAddCardClicked)
        onAddCardClicked();

    // Clear drag-source dim
    if (isDragSource)
    {
        isDragSource = false;
        repaint();
    }
    dragStarted  = false;
    canStartDrag = false;
}

bool CognitoniBlkFxAudioProcessorEditor::CardComponent::isInterestedInDragSource (
    const juce::DragAndDropTarget::SourceDetails& details)
{
    // Accept drops from other cards (not ourselves)
    return details.description.toString().startsWith ("card:")
        && details.sourceComponent != this;
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::itemDragEnter (
    const juce::DragAndDropTarget::SourceDetails&)
{
    isDragTarget = true;
    repaint();
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::itemDragExit (
    const juce::DragAndDropTarget::SourceDetails&)
{
    isDragTarget = false;
    repaint();
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::itemDropped (
    const juce::DragAndDropTarget::SourceDetails& details)
{
    isDragTarget = false;
    repaint();
    const int srcSlot = details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    // Clear drag-source state on the originating card
    if (auto* srcCard = dynamic_cast<CardComponent*> (details.sourceComponent.get()))
    {
        srcCard->isDragSource = false;
        srcCard->repaint();
    }
    if (onDragReorder && srcSlot != slotIndex)
        onDragReorder (srcSlot, slotIndex);
}

//  LookAndFeel 
CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::CognitoniLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour::fromRGB (80,  160, 210));
    setColour (juce::Slider::thumbColourId,             juce::Colour::fromRGB (90,  175, 220));
    setColour (juce::Slider::trackColourId,             juce::Colour::fromRGB (175, 170, 162));
    setColour (juce::Slider::textBoxTextColourId,       juce::Colour::fromRGB (45,  50,  58));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour::fromRGB (200, 195, 188));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB (248, 244, 238));
    setColour (juce::ComboBox::backgroundColourId,      juce::Colour::fromRGB (248, 244, 238));
    setColour (juce::ComboBox::textColourId,            juce::Colour::fromRGB (38,  42,  50));
    setColour (juce::ComboBox::outlineColourId,         juce::Colour::fromRGB (195, 190, 182));
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour::fromRGB (248, 244, 238));
    setColour (juce::PopupMenu::textColourId,           juce::Colour::fromRGB (38,  42,  50));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour::fromRGB (205, 200, 192));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour::fromRGB (20,  20,  20));
    // AlertWindow theme
    setColour (juce::AlertWindow::backgroundColourId,          juce::Colour::fromRGB (228, 224, 216));
    setColour (juce::AlertWindow::textColourId,                juce::Colour::fromRGB (38,  42,  50));
    setColour (juce::AlertWindow::outlineColourId,             juce::Colour::fromRGB (190, 185, 178));
    setColour (juce::TextButton::buttonColourId,               juce::Colour::fromRGB (205, 200, 193));
    setColour (juce::TextButton::textColourOffId,              juce::Colour::fromRGB (38,  42,  50));
    setColour (juce::TextEditor::backgroundColourId,           juce::Colour::fromRGB (248, 244, 238));
    setColour (juce::TextEditor::textColourId,                 juce::Colour::fromRGB (38,  42,  50));
    setColour (juce::TextEditor::outlineColourId,              juce::Colour::fromRGB (195, 190, 182));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)width, (float)height).reduced (5.0f);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto radius = side * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = juce::jmap (sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
    const auto accentCol = slider.findColour (juce::Slider::rotarySliderFillColourId);

    const auto trackR = radius - 5.0f;
    { juce::Path track; track.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
      g.setColour (juce::Colour::fromRGB (190, 185, 178));
      g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }

    { juce::Path arc; arc.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, rotaryStartAngle, angle, true);
      g.setColour (accentCol);
      g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }

    const auto capR    = radius - 10.0f;
    auto capBounds     = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (capBounds.expanded (2.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (242, 238, 233), centre.translated (-capR * 0.3f, -capR * 0.3f),
                                             juce::Colour::fromRGB (218, 214, 208), centre.translated ( capR * 0.3f,  capR * 0.3f), false));
    g.fillEllipse (capBounds);
    g.setColour (juce::Colour::fromRGB (192, 188, 182));
    g.drawEllipse (capBounds.reduced (0.5f), 1.0f);

    const auto dotDist = capR - 5.0f;
    const auto dotPt   = centre + juce::Point<float> (std::cos (angle - juce::MathConstants<float>::halfPi) * dotDist,
                                                      std::sin (angle - juce::MathConstants<float>::halfPi) * dotDist);
    g.setColour (accentCol);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (dotPt));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawLinearSlider (
    juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused (sliderPos, slider);

    if (style != juce::Slider::TwoValueHorizontal && style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        const auto centreX  = (float)(x + width / 2);
        const auto trackTop = (float)(y + 8);
        const auto trackBot = (float)(y + height - 8);
        const auto trackW   = 5.0f;
        const auto thumbH   = 18.0f;
        const auto thumbW   = juce::jmin ((float)width - 4.0f, 38.0f);
        g.setColour (juce::Colour::fromRGB (58, 66, 78));
        g.fillRoundedRectangle (centreX - trackW * 0.5f, trackTop, trackW, trackBot - trackTop, trackW * 0.5f);
        g.setColour (juce::Colour::fromRGB (100, 160, 220).withAlpha (0.8f));
        g.fillRoundedRectangle (centreX - trackW * 0.5f, sliderPos, trackW, trackBot - sliderPos, trackW * 0.5f);
        auto thumbBounds = juce::Rectangle<float> (centreX - thumbW * 0.5f, sliderPos - thumbH * 0.5f, thumbW, thumbH);
        g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (228, 224, 218), thumbBounds.getTopLeft(),
                                                 juce::Colour::fromRGB (205, 200, 193), thumbBounds.getBottomLeft(), false));
        g.fillRoundedRectangle (thumbBounds, 4.0f);
        g.setColour (juce::Colour::fromRGB (178, 175, 169));
        g.drawRoundedRectangle (thumbBounds.reduced (0.5f), 4.0f, 1.0f);
        const float gx = thumbBounds.getCentreX(), gy = thumbBounds.getCentreY();
        for (int i = -1; i <= 1; ++i)
        {
            g.setColour (juce::Colour::fromRGB (148, 144, 138));
            g.drawLine (gx - 8.0f, gy + (float)i * 4.5f, gx + 8.0f, gy + (float)i * 4.5f, 1.0f);
        }
        return;
    }

    const auto centreY = (float)(y + height / 2);
    const auto left    = (float)(x + 8);
    const auto right   = (float)(x + width - 8);
    g.setColour (juce::Colour::fromRGB (185, 180, 174));
    g.drawLine (left, centreY, right, centreY, 4.0f);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.drawLine (minSliderPos, centreY, maxSliderPos, centreY, 4.0f);
    const auto thumbSize = 10.0f;
    g.fillEllipse (juce::Rectangle<float> (thumbSize, thumbSize).withCentre ({ minSliderPos, centreY }));
    g.fillEllipse (juce::Rectangle<float> (thumbSize, thumbSize).withCentre ({ maxSliderPos, centreY }));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawComboBox (
    juce::Graphics& g, int width, int height, bool isButtonDown,
    int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown);
    const auto bounds = juce::Rectangle<float> (0, 0, (float)width, (float)height);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);
    const auto cx = (float)(buttonX + buttonW / 2), cy = (float)(buttonY + buttonH / 2);
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (juce::Colour::fromRGB (95, 100, 110));
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawToggleButton (
    juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    auto bounds = button.getLocalBounds().toFloat().reduced (1.5f);
    const bool active  = ! button.getToggleState();
    const auto accentCol = button.findColour (juce::ToggleButton::tickColourId);
    g.setColour (juce::Colour::fromRGB (145, 140, 135));
    g.drawEllipse (bounds.reduced (0.5f), 1.5f);
    auto inner = bounds.reduced (3.5f);
    g.setColour (active ? accentCol : juce::Colour::fromRGB (205, 200, 195));
    g.fillEllipse (inner);
}

juce::Label* CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    // Subtle border on the value box
    label->setColour (juce::Label::outlineColourId, juce::Colour::fromRGB (200, 195, 188));
    return label;
}

void CognitoniBlkFxAudioProcessorEditor::configureAmountKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);  // reduced height: closer to knob
    slider.setRange (0.0, 1.0, 0.001);
}

void CognitoniBlkFxAudioProcessorEditor::configureRangeSlider (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::TwoValueHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0, 0.001);
}

//  Helper: normalised Hz display text 
juce::String CognitoniBlkFxAudioProcessorEditor::normalisedToHzText (double v) const
{
    const float hz = static_cast<float> (normalisedToDtBlkHzUi (static_cast<double> (v),
                                            static_cast<double> (audioProcessor.getCurrentNyquistHz())));
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String CognitoniBlkFxAudioProcessorEditor::normalisedToPercentText (double v)
{
    return juce::String (juce::roundToInt (v * 100.0)) + "%";
}

void CognitoniBlkFxAudioProcessorEditor::setParameterNormalised (const juce::String& pid, float v)
{
    if (auto* p = audioProcessor.getAPVTS().getParameter (pid))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

//  Main editor constructor 
CognitoniBlkFxAudioProcessorEditor::CognitoniBlkFxAudioProcessorEditor (CognitoniBlkFxAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Load embedded Montserrat typefaces (once per editor instance)
    montserratRegular = juce::Typeface::createSystemTypefaceFor (
        EmbeddedFonts::Montserrat_Regular_ttf, EmbeddedFonts::Montserrat_Regular_ttf_size);
    montserratBold = juce::Typeface::createSystemTypefaceFor (
        EmbeddedFonts::Montserrat_Bold_ttf, EmbeddedFonts::Montserrat_Bold_ttf_size);

    setLookAndFeel (&lookAndFeel);
    setSize (760, 660);
    setResizeLimits (640, 640, 1200, 900);

    auto& apvts = audioProcessor.getAPVTS();

    //  header labels 
    pluginNameLabel.setText ("blkfx", juce::dontSendNotification);
    pluginNameLabel.setFont (juce::Font (juce::FontOptions{}.withTypeface (montserratBold).withHeight (36.0f)));
    pluginNameLabel.setJustificationType (juce::Justification::centredLeft);
    pluginNameLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (38, 42, 50));
    addAndMakeVisible (pluginNameLabel);

    presetLabel.setText ("Preset:", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratRegular).withHeight (11.0f)));
    presetLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 96, 92));
    addAndMakeVisible (presetLabel);

    refreshPresetSelectorItems();
    addAndMakeVisible (presetSelector);
    presetSelector.onChange = [this]
    {
        const auto selected = presetSelector.getSelectedId();
        if (selected > 0)
        {
            audioProcessor.applyPresetByIndex (selected - 1);
            lastAppliedPresetSelectorId = audioProcessor.getCurrentPresetIndex() + 1;
            presetSelector.setSelectedId (lastAppliedPresetSelectorId, juce::dontSendNotification);
            deletePresetButton.setEnabled (audioProcessor.isPresetUserDeletable (audioProcessor.getCurrentPresetIndex()));
        }
    };

    savePresetButton.setSvgIcon   (EmbeddedIcons::saveSvg);
    deletePresetButton.setSvgIcon (EmbeddedIcons::deleteSvg);
    randomizeButton.setSvgIcon    (EmbeddedIcons::randomSvg);

    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (deletePresetButton);
    addAndMakeVisible (randomizeButton);

    savePresetButton.onClick   = [this] { showSavePresetDialog(); };
    deletePresetButton.onClick = [this] { showDeletePresetDialog(); };
    randomizeButton.onClick    = [this] { randomizeCards(); };

    versionLabel.setButtonText ("v0.1.0");
    versionLabel.setURL (juce::URL ("https://github.com/toni-lyttinen/CognitoniBlkFx/tags"));
    versionLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratRegular).withHeight (16.0f)), false, juce::Justification::centred);
    versionLabel.setColour (juce::HyperlinkButton::textColourId, juce::Colour::fromRGB (148, 144, 138));
    addAndMakeVisible (versionLabel);

    //  slot cards 
    for (int s = 0; s < CardSchema::numSlots; ++s)
    {
        auto& card = slotCards[s];
        card.slotIndex = s;
        card.setTypefaces (montserratRegular, montserratBold);  // pass embedded fonts
        addAndMakeVisible (card);

        configureAmountKnob  (card.amountKnob);
        configureAmountKnob  (card.wetDryKnob);
        configureRangeSlider (card.frequencyRangeSlider);

        card.amountKnob.textFromValueFunction = normalisedToDbText;
        card.amountKnob.setDoubleClickReturnValue (true, 0.6);  // double-click resets to 0 dB
        card.wetDryKnob.textFromValueFunction = normalisedToPercentText;
        card.frequencyRangeSlider.textFromValueFunction = [this](double v){ return normalisedToHzText (v); };

        card.frequencyRangeSlider.onValueChange = [this] { pushRangeSliderToParams(); };

        // Add card title as mouse listener to enable drag from title area
        card.title.addMouseListener (&card, false);
        card.bypassButton.addMouseListener (&card, false);
        card.removeButton.addMouseListener (&card, false);

        // Whole card click opens picker
        card.onAddCardClicked = [this, s] { showCardPickerModal (s); };

        // Drag reorder: swap parameters between two slots
        card.onDragReorder = [this] (int src, int dst)
        {
            if (src < 0 || dst < 0 || src >= CardSchema::numSlots || dst >= CardSchema::numSlots || src == dst)
                return;
            // Read both slots' parameters
            auto& apvts = audioProcessor.getAPVTS();
            auto readSlot = [&] (int slot) {
                std::array<float, 7> vals {};
                if (auto* p = apvts.getRawParameterValue (CardSchema::cardTypeParam (slot))) vals[0] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::amountParam   (slot))) vals[1] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::harmTypeParam (slot))) vals[2] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::freqAParam    (slot))) vals[3] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::freqBParam    (slot))) vals[4] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::bypassParam   (slot))) vals[5] = p->load();
                if (auto* p = apvts.getRawParameterValue (CardSchema::wetDryParam   (slot))) vals[6] = p->load();
                return vals;
            };
            auto srcVals = readSlot (src);
            auto dstVals = readSlot (dst);
            // Write swapped values
            auto writeSlot = [&] (int slot, const std::array<float,7>& v) {
                setParameterNormalised (CardSchema::cardTypeParam (slot), v[0] / (float)(CardSchema::numCardTypes - 1));
                setParameterNormalised (CardSchema::amountParam   (slot), v[1]);
                setParameterNormalised (CardSchema::harmTypeParam (slot), v[2] / 3.0f);
                setParameterNormalised (CardSchema::freqAParam    (slot), v[3]);
                setParameterNormalised (CardSchema::freqBParam    (slot), v[4]);
                setParameterNormalised (CardSchema::bypassParam   (slot), v[5]);
                setParameterNormalised (CardSchema::wetDryParam   (slot), v[6]);
            };
            writeSlot (src, dstVals);
            writeSlot (dst, srcVals);
        };

        card.removeButton.onClick  = [this, s]
        {
            setParameterNormalised (CardSchema::cardTypeParam (s), 0.0f);
        };

        slotFreqAParams[s] = apvts.getRawParameterValue (CardSchema::freqAParam (s));
        slotFreqBParams[s] = apvts.getRawParameterValue (CardSchema::freqBParam (s));
    }

    //  right panel 
    addAndMakeVisible (inputLevelMeter);
    addAndMakeVisible (outputLevelMeter);
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (outputMeterLabel);
    inputMeterLabel.setText  ("IN",  juce::dontSendNotification);
    outputMeterLabel.setText ("OUT", juce::dontSendNotification);
    inputMeterLabel.setJustificationType  (juce::Justification::centred);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    inputMeterLabel.setColour  (juce::Label::textColourId, juce::Colour::fromRGB (170, 178, 195));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 178, 195));

    configureAmountKnob (inputGainKnob);
    configureAmountKnob (outputGainKnob);
    configureAmountKnob (masterWetDryKnob);
    // Narrow IN/OUT gain boxes (they don't need as much space as card knobs)
    inputGainKnob.setTextBoxStyle  (juce::Slider::TextBoxBelow, false, 56, 18);
    outputGainKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);

    inputGainKnob.textFromValueFunction    = normalisedToDbText;
    outputGainKnob.textFromValueFunction   = normalisedToDbText;
    masterWetDryKnob.textFromValueFunction = normalisedToPercentText;

    addAndMakeVisible (inputGainKnob);
    addAndMakeVisible (outputGainKnob);
    addAndMakeVisible (masterWetDryKnob);

    masterWetDryLabel.setText ("Mix", juce::dontSendNotification);
    masterWetDryLabel.setJustificationType (juce::Justification::centred);
    masterWetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 178, 195));
    addAndMakeVisible (masterWetDryLabel);

    inputGainAttachment    = std::make_unique<SliderAttachment> (apvts, "inputGain",    inputGainKnob);
    outputGainAttachment   = std::make_unique<SliderAttachment> (apvts, "outputGain",   outputGainKnob);
    masterWetDryAttachment = std::make_unique<SliderAttachment> (apvts, "masterWetDry", masterWetDryKnob);
    masterWetDryKnob.forceTextBoxUpdate();  // ensure textFromValueFunction is applied on first draw

    // BlackLens knob — FFT window size (ms, 5–1830, displayed as continuous ms/sec)
    configureAmountKnob (blackLensKnob);
    blackLensKnob.setDoubleClickReturnValue (true, 92.2);
    blackLensAttachment = std::make_unique<SliderAttachment> (apvts, paramBlackLens, blackLensKnob);
    blackLensKnob.textFromValueFunction = [](double valueMs) -> juce::String
    {
        if (valueMs >= 1000.0)
            return juce::String (valueMs / 1000.0, 2) + " sec";
        return juce::String (valueMs, 1) + " ms";
    };
    blackLensKnob.forceTextBoxUpdate();

    blackLensLabel.setText ("BlackLens", juce::dontSendNotification);
    blackLensLabel.setJustificationType (juce::Justification::centred);
    blackLensLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 178, 195));
    addAndMakeVisible (blackLensKnob);
    addAndMakeVisible (blackLensLabel);

    startTimerHz (30);
}

CognitoniBlkFxAudioProcessorEditor::~CognitoniBlkFxAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//  Main editor paint 
void CognitoniBlkFxAudioProcessorEditor::mouseDown (const juce::MouseEvent& /*e*/)
{
    // Card picker overlay handles its own dismiss via full-screen backdrop mouseDown
}

void CognitoniBlkFxAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Outer grey-beige background
    g.fillAll (juce::Colour::fromRGB (205, 200, 193));

    // Main warm cream panel (the whole interior minus outer margin)
    const auto panel = getLocalBounds().toFloat().reduced (10.0f);

    // Drop shadow
    g.setColour (juce::Colour::fromRGBA (100, 96, 90, 55));
    g.fillRoundedRectangle (panel.translated (0.0f, 5.0f), 18.0f);

    // Main gradient fill
    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (249, 245, 240), panel.getTopLeft(),
                                             juce::Colour::fromRGB (240, 236, 230), panel.getBottomLeft(), false));
    g.fillRoundedRectangle (panel, 18.0f);
    g.setColour (juce::Colour::fromRGB (225, 220, 213).withAlpha (0.8f));
    g.drawRoundedRectangle (panel.reduced (0.75f), 18.0f, 1.5f);

    // Right side panel (dark)
    if (! rightPanelRect.isEmpty())
    {
        auto rPanel = rightPanelRect.toFloat();
        g.setColour (juce::Colour::fromRGB (42, 48, 56));
        g.fillRoundedRectangle (rPanel, 14.0f);
        g.setColour (juce::Colour::fromRGB (58, 65, 75));
        g.drawRoundedRectangle (rPanel.reduced (0.5f), 14.0f, 1.0f);
    }
}

//  Main editor resized 
void CognitoniBlkFxAudioProcessorEditor::resized()
{
    const int margin       = 22;
    const int sidePanelW   = 148;
    const int sidePanelGap = 14;
    const int rowGap       = 6;
    const int iconSz       = 26;

    const float sf = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);

    // Scale fonts (Montserrat)
    pluginNameLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratBold)
                             .withHeight (juce::jmax (20.0f, 36.0f * sf))));
    pluginNameLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (38, 42, 50));
    presetLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratRegular)
                         .withHeight (juce::jmax (9.0f, 11.0f * sf))));
    versionLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratRegular)
                          .withHeight (juce::jmax (8.5f, 10.5f * sf))), false, juce::Justification::centred);
    inputMeterLabel.setFont  (juce::Font (juce::FontOptions().withTypeface (montserratBold).withHeight (juce::jmax (9.0f, 11.0f * sf))));
    outputMeterLabel.setFont (juce::Font (juce::FontOptions().withTypeface (montserratBold).withHeight (juce::jmax (9.0f, 11.0f * sf))));

    auto full = getLocalBounds().reduced (margin);

    // Right dark panel (taken first so its bounds are consistent with paint())
    auto rightPanel = full.removeFromRight (sidePanelW);
    // Trim top so the panel aligns with the preset/action buttons top
    rightPanel = rightPanel.withTrimmedTop ((50 - 6 - iconSz) / 2);
    full.removeFromRight (sidePanelGap);
    rightPanelRect = rightPanel;

    // Header row (in main card area only, NOT over right panel)
    auto header = full.removeFromTop (50);
    header.removeFromBottom (6);

    // Issue 6: title left edge aligned with first card's left edge (full.getX())
    // Use explicit bounds rather than removeFromLeft so the area is at the exact card X
    const int titleW = 160;
    pluginNameLabel.setBounds (full.getX(), header.getY() - 5, titleW, header.getHeight() + 10);
    header.removeFromLeft (titleW + 16);  // consume same space from header for the preset row

    auto presetRow = header;
    // Right-align: delete → randomize (right of delete) → save → selector → label
    deletePresetButton.setBounds (presetRow.removeFromRight (iconSz).withSizeKeepingCentre (iconSz, iconSz));
    presetRow.removeFromRight (rowGap / 2);
    randomizeButton.setBounds (presetRow.removeFromRight (iconSz).withSizeKeepingCentre (iconSz, iconSz));
    presetRow.removeFromRight (rowGap);
    savePresetButton.setBounds (presetRow.removeFromRight (iconSz).withSizeKeepingCentre (iconSz, iconSz));
    presetRow.removeFromRight (rowGap);
    const int desiredBoxWidth = juce::jmin (160, (int)(presetRow.getWidth() * 0.65f));
    auto selectorArea = presetRow.removeFromRight (desiredBoxWidth);
    presetSelector.setBounds (selectorArea.withSizeKeepingCentre (desiredBoxWidth, 24));
    presetLabel.setBounds (presetRow.removeFromRight (52).withSizeKeepingCentre (52, 16));

    full.removeFromTop (6);   // gap between header and cards

    // Issue 5: balance top/bottom padding. Header (50px) + gap (6px) = 56px overhead above cards.
    // Remove 28px from bottom to equalise so each side ends up with margin + 28 px
    full.removeFromBottom (28);

    // Card slots in remaining area
    const int cardGap   = 10;
    const int cardWidth = (full.getWidth() - cardGap * 2) / 3;
    for (int s = 0; s < CardSchema::numSlots; ++s)
        slotCards[s].setBounds (full.getX() + s * (cardWidth + cardGap), full.getY(),
                                cardWidth, full.getHeight());

    // Right panel internals
    versionLabel.setBounds (rightPanel.removeFromBottom (16).reduced (4, 2));

    auto rp = rightPanel.reduced (12, 14);

    const int labelH  = 14;
    const int rowGapI = 4;
    const int colW    = (rp.getWidth() - 8) / 2;

    // IN / OUT column headers
    auto topLabels = rp.removeFromTop (labelH);
    rp.removeFromTop (4);
    inputMeterLabel.setBounds  (topLabels.removeFromLeft (colW + 4));
    outputMeterLabel.setBounds (topLabels);

    // Level meters — fills space above gain knobs and below-knob controls
    const int gainKnobH    = 66;
    const int perKnobAreaH = labelH + rowGapI + 52 + 16;          // one label+knob row
    const int bottomKnobsH = gainKnobH + (rowGapI + 4)            // gain knobs + gap
                             + 2 * perKnobAreaH + 4 + 24;         // blackLens + mix + pads
    const int meterH       = rp.getHeight() - bottomKnobsH - 6;
    auto metersArea = rp.removeFromTop (juce::jmax (30, meterH));
    rp.removeFromTop (6);

    inputLevelMeter.setBounds  (metersArea.removeFromLeft (colW + 4).reduced (3, 0));
    outputLevelMeter.setBounds (metersArea.reduced (3, 0));

    // Gain knobs (now below meters)
    auto gainRow = rp.removeFromTop (gainKnobH);
    inputGainKnob.setBounds  (gainRow.removeFromLeft (colW + 4));
    outputGainKnob.setBounds (gainRow);
    rp.removeFromTop (rowGapI + 4);

    // BlackLens knob
    const int blKnobSz = juce::jmin (rp.getWidth() - 8, 52);
    const int blKnobH  = blKnobSz + 16;
    blackLensLabel.setBounds (rp.removeFromTop (labelH));
    rp.removeFromTop (rowGapI);
    auto blArea = rp.removeFromTop (blKnobH);
    blackLensKnob.setBounds (blArea.withSizeKeepingCentre (blKnobSz + 16, blKnobH));
    rp.removeFromTop (4);

    // Mix knob
    const int mixKnobSz = juce::jmin (rp.getWidth() - 8, 52);
    masterWetDryLabel.setBounds (rp.removeFromTop (labelH));
    rp.removeFromTop (rowGapI);
    const int mixKnobH = mixKnobSz + 16;
    auto mixArea = rp.removeFromTop (mixKnobH);
    masterWetDryKnob.setBounds (mixArea.withSizeKeepingCentre (mixKnobSz + 16, mixKnobH));

    repaint();
}

//  Timer callback 
void CognitoniBlkFxAudioProcessorEditor::timerCallback()
{
    // Update level meters
    inputLevelMeter.setLevel  (audioProcessor.getLastInputRms());
    outputLevelMeter.setLevel (audioProcessor.getLastOutputRms());
    inputLevelMeter.repaint();
    outputLevelMeter.repaint();

    // Detect slot cardType changes  rebuild UI
    auto& apvts = audioProcessor.getAPVTS();
    for (int s = 0; s < CardSchema::numSlots; ++s)
    {
        if (auto* p = apvts.getRawParameterValue (CardSchema::cardTypeParam (s)))
        {
            const int t = static_cast<int> (p->load (std::memory_order_relaxed) + 0.5f);
            if (t != lastKnownCardTypes[s])
            {
                lastKnownCardTypes[s] = t;
                slotCards[s].setCardType (static_cast<CardSchema::CardType> (t));
                rebuildSlotAttachments (s);
            }
        }
    }

    // Sync freq range sliders from params (avoids missing slider changes)
    syncRangeSlidersFromParams();
    refreshFrequencyLabels();

    // Sync preset selector
    if (! presetSelector.isPopupActive())
    {
        const int procPreset = audioProcessor.getCurrentPresetIndex();
        if (procPreset + 1 != presetSelector.getSelectedId())
        {
            presetSelector.setSelectedId (procPreset + 1, juce::dontSendNotification);
            lastAppliedPresetSelectorId = procPreset + 1;
        }
    }
}

void CognitoniBlkFxAudioProcessorEditor::refreshFrequencyLabels()
{
    for (int s = 0; s < CardSchema::numSlots; ++s)
    {
        auto& card = slotCards[s];
        if (card.getCardType() == CardSchema::CardType::empty) continue;

        const double lo = card.frequencyRangeSlider.getMinValue();
        const double hi = card.frequencyRangeSlider.getMaxValue();
        card.frequencyALabel.setText (normalisedToHzText (lo), juce::dontSendNotification);
        card.frequencyBLabel.setText (normalisedToHzText (hi), juce::dontSendNotification);
    }
}

void CognitoniBlkFxAudioProcessorEditor::syncRangeSlidersFromParams()
{
    if (updatingRangeControls) return;
    const juce::ScopedValueSetter<bool> scope (updatingRangeControls, true);

    for (int s = 0; s < CardSchema::numSlots; ++s)
    {
        auto& card = slotCards[s];
        if (card.getCardType() == CardSchema::CardType::empty) continue;

        if (slotFreqAParams[s] != nullptr && slotFreqBParams[s] != nullptr)
        {
            const double a = static_cast<double> (slotFreqAParams[s]->load (std::memory_order_relaxed));
            const double b = static_cast<double> (slotFreqBParams[s]->load (std::memory_order_relaxed));
            card.frequencyRangeSlider.setMinAndMaxValues (a, b, juce::dontSendNotification);
        }
    }
}

void CognitoniBlkFxAudioProcessorEditor::pushRangeSliderToParams()
{
    if (updatingRangeControls) return;
    const juce::ScopedValueSetter<bool> scope (updatingRangeControls, true);

    for (int s = 0; s < CardSchema::numSlots; ++s)
    {
        auto& card = slotCards[s];
        if (card.getCardType() == CardSchema::CardType::empty) continue;

        setParameterNormalised (CardSchema::freqAParam (s),
                                static_cast<float> (card.frequencyRangeSlider.getMinValue()));
        setParameterNormalised (CardSchema::freqBParam (s),
                                static_cast<float> (card.frequencyRangeSlider.getMaxValue()));
    }
}

void CognitoniBlkFxAudioProcessorEditor::refreshPresetSelectorItems()
{
    presetSelector.clear (juce::dontSendNotification);
    const auto names = audioProcessor.getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetSelector.addItem (names[i], i + 1);
    lastAppliedPresetSelectorId = audioProcessor.getCurrentPresetIndex() + 1;
    presetSelector.setSelectedId (lastAppliedPresetSelectorId,
                                  juce::dontSendNotification);
}

//  Slot attachment management 
void CognitoniBlkFxAudioProcessorEditor::rebuildSlotAttachments (int s)
{
    // Destroy old attachments before re-creating to avoid double-attachment
    slotAttachments[s] = {};

    const auto type = slotCards[s].getCardType();
    if (type == CardSchema::CardType::empty)
        return;

    auto& apvts = audioProcessor.getAPVTS();

    // For AutoHarm/Contrast/Saws: attach amountKnob to wetDryParam (amplitude = dB on top)
    // and wetDryKnob to amountParam (card-specific value on bottom), preserving HEAD semantics.
    const bool swapKnobs = (type == CardSchema::CardType::autoHarm
                            || type == CardSchema::CardType::contrast
                            || type == CardSchema::CardType::saws);
    if (swapKnobs)
    {
        slotAttachments[s].amount = std::make_unique<SliderAttachment> (apvts, CardSchema::wetDryParam (s), slotCards[s].amountKnob);
        slotAttachments[s].wetDry = std::make_unique<SliderAttachment> (apvts, CardSchema::amountParam (s), slotCards[s].wetDryKnob);
    }
    else
    {
        slotAttachments[s].amount = std::make_unique<SliderAttachment> (apvts, CardSchema::amountParam (s), slotCards[s].amountKnob);
        slotAttachments[s].wetDry = std::make_unique<SliderAttachment> (apvts, CardSchema::wetDryParam (s), slotCards[s].wetDryKnob);
    }
    slotAttachments[s].bypass = std::make_unique<ButtonAttachment> (apvts, CardSchema::bypassParam (s), slotCards[s].bypassButton);

    // Set formatters and labels after attachment creation (JUCE SliderAttachment may reset them).
    auto& card = slotCards[s];
    switch (type)
    {
        case CardSchema::CardType::autoHarm:
            card.amountHeaderLabel.setText ("dB", juce::dontSendNotification);
            card.amountKnob.textFromValueFunction = normalisedToDbText;
            card.amountKnob.setDoubleClickReturnValue (true, 0.6);
            card.wetDryHeaderLabel.setText ("Type", juce::dontSendNotification);
            card.wetDryKnob.textFromValueFunction = autoHarmValueText;
            card.wetDryKnob.setDoubleClickReturnValue (true, 0.325);
            break;

        case CardSchema::CardType::contrast:
            card.amountHeaderLabel.setText ("dB", juce::dontSendNotification);
            card.amountKnob.textFromValueFunction = normalisedToDbText;
            card.amountKnob.setDoubleClickReturnValue (true, 0.6);
            card.wetDryHeaderLabel.setText ("Value", juce::dontSendNotification);
            card.wetDryKnob.textFromValueFunction = contrastValueText;
            card.wetDryKnob.setDoubleClickReturnValue (true, 0.5);
            break;

        case CardSchema::CardType::saws:
            card.amountHeaderLabel.setText ("dB", juce::dontSendNotification);
            card.amountKnob.textFromValueFunction = normalisedToDbText;
            card.amountKnob.setDoubleClickReturnValue (true, 0.6);
            card.wetDryHeaderLabel.setText ("Value", juce::dontSendNotification);
            card.wetDryKnob.textFromValueFunction = sawsValueText;
            card.wetDryKnob.setDoubleClickReturnValue (true, 0.0);
            break;

        case CardSchema::CardType::smear:
        default:
            card.amountHeaderLabel.setText ("dB", juce::dontSendNotification);
            card.amountKnob.textFromValueFunction = normalisedToDbText;
            card.amountKnob.setDoubleClickReturnValue (true, 0.6);
            card.wetDryKnob.textFromValueFunction = normalisedToPercentText;
            card.wetDryKnob.setDoubleClickReturnValue (true, 0.0);
            break;
    }

    // Force text boxes to re-render with the restored formatters
    card.amountKnob.forceTextBoxUpdate();
    card.wetDryKnob.forceTextBoxUpdate();
}

//  Card picker overlay 
CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::CardPickerOverlay()
{
    // Size is set by showCardPickerModal; we cover the whole editor for backdrop/dismiss
    setInterceptsMouseClicks (true, true);
}

juce::Rectangle<float> CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::itemRect (int idx) const
{
    // 2×2 grid inside popupRect, below a title row
    const float titleH = 34.0f;
    const float pad    = 14.0f;
    const float gap    = 10.0f;
    const float cW     = (popupRect.getWidth() - pad * 2.0f - gap) * 0.5f;
    const float cH     = (popupRect.getHeight() - titleH - pad * 2.0f - gap) * 0.5f;
    const float col    = (float)(idx % 2);
    const float row    = (float)(idx / 2);
    const float x0     = popupRect.getX() + pad + col * (cW + gap);
    const float y0     = popupRect.getY() + titleH + pad + row * (cH + gap);
    return { x0, y0, cW, cH };
}

void CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::paint (juce::Graphics& g)
{
    // Semi-transparent backdrop (dismiss on click outside)
    g.setColour (juce::Colour::fromRGBA (20, 20, 28, 140));
    g.fillRect (getLocalBounds());

    if (popupRect.isEmpty())
        return;

    // Popup box background
    g.setColour (juce::Colour::fromRGB (248, 244, 238));
    g.fillRoundedRectangle (popupRect, 14.0f);
    g.setColour (juce::Colour::fromRGB (205, 200, 192));
    g.drawRoundedRectangle (popupRect.reduced (0.5f), 14.0f, 1.5f);

    // Title "ADD CARD"
    g.setColour (juce::Colour::fromRGB (40, 44, 52));
    auto titleFont = boldTypeface != nullptr
        ? juce::Font (juce::FontOptions{}.withTypeface (boldTypeface).withHeight (13.0f))
        : juce::Font (juce::FontOptions{}.withName ("Segoe UI").withHeight (13.0f).withStyle ("Bold"));
    g.setFont (titleFont);
    const float titleH = 34.0f;
    g.drawText ("ADD CARD", popupRect.withHeight (titleH), juce::Justification::centred, false);

    // Separator
    g.setColour (juce::Colour::fromRGB (220, 215, 208));
    g.drawLine (popupRect.getX() + 14.0f, popupRect.getY() + titleH,
                popupRect.getRight() - 14.0f, popupRect.getY() + titleH, 1.0f);

    const CardSchema::CardType types[] = {
        CardSchema::CardType::autoHarm, CardSchema::CardType::contrast,
        CardSchema::CardType::saws,     CardSchema::CardType::smear };
    const char* names[] = { "AUTOHARM", "CONTRAST", "SAWS", "SMEAR" };

    for (int i = 0; i < 4; ++i)
    {
        auto r = itemRect (i);
        const auto accent = CardSchema::accentColour (types[i]);
        const bool hovered = (hoveredItem == i);

        // Cell background
        g.setColour (hovered ? accent.withAlpha (0.14f) : juce::Colour::fromRGB (243, 239, 233));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (hovered ? accent.withAlpha (0.6f) : juce::Colour::fromRGB (215, 210, 203));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);

        // Icon area (upper ~60% of cell)
        const float iconAreaH = r.getHeight() * 0.58f;
        auto iconArea = r.withHeight (iconAreaH).reduced (9.0f, 7.0f);
        g.setColour (accent.withAlpha (0.08f));
        g.fillRoundedRectangle (iconArea, 6.0f);

        // Draw card icon
        const float iconInset = 6.0f;
        auto drawRect = iconArea.reduced (iconInset, iconInset * 0.5f);
        switch (types[i])
        {
            case CardSchema::CardType::autoHarm: CardComponent::drawAutoHarmIcon (g, drawRect, accent); break;
            case CardSchema::CardType::contrast: CardComponent::drawContrastIcon (g, drawRect, accent); break;
            case CardSchema::CardType::saws:     CardComponent::drawSawsIcon     (g, drawRect, accent); break;
            case CardSchema::CardType::smear:    CardComponent::drawSmearIcon    (g, drawRect, accent); break;
            default: break;
        }

        // Name label (bottom section of cell)
        g.setColour (hovered ? accent.darker (0.25f) : juce::Colour::fromRGB (38, 42, 50));
        auto nameFont = boldTypeface != nullptr
            ? juce::Font (juce::FontOptions{}.withTypeface (boldTypeface).withHeight (11.5f))
            : juce::Font (juce::FontOptions{}.withName ("Segoe UI").withHeight (11.5f).withStyle ("SemiBold"));
        g.setFont (nameFont);
        auto nameRect = r.withTrimmedTop (iconAreaH).reduced (4.0f, 2.0f);
        g.drawText (names[i], nameRect, juce::Justification::centred, false);
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::mouseMove (const juce::MouseEvent& e)
{
    int newHover = -1;
    if (popupRect.contains ((float)e.x, (float)e.y))
        for (int i = 0; i < 4; ++i)
            if (itemRect (i).contains ((float)e.x, (float)e.y))
                newHover = i;
    if (newHover != hoveredItem)
    {
        hoveredItem = newHover;
        repaint();
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::mouseExit (const juce::MouseEvent&)
{
    hoveredItem = -1;
    repaint();
}

void CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::mouseDown (const juce::MouseEvent& e)
{
    // Click outside popup → dismiss
    if (! popupRect.contains ((float)e.x, (float)e.y))
    {
        if (onDismiss) onDismiss();
    }
}

void CognitoniBlkFxAudioProcessorEditor::CardPickerOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (! popupRect.contains ((float)e.x, (float)e.y))
        return;

    const CardSchema::CardType types[] = {
        CardSchema::CardType::autoHarm, CardSchema::CardType::contrast,
        CardSchema::CardType::saws,     CardSchema::CardType::smear };

    for (int i = 0; i < 4; ++i)
    {
        if (itemRect (i).contains ((float)e.x, (float)e.y))
        {
            if (onCardChosen)
                onCardChosen (types[i]);
            return;
        }
    }
}

//  Card picker modal launch 
void CognitoniBlkFxAudioProcessorEditor::showCardPickerModal (int s)
{
    // Dismiss any existing overlay
    if (cardPickerOverlay != nullptr)
    {
        removeChildComponent (cardPickerOverlay.get());
        cardPickerOverlay.reset();
    }

    cardPickerOverlay = std::make_unique<CardPickerOverlay>();
    cardPickerOverlay->setTypefaces (montserratRegular, montserratBold);  // pass embedded fonts

    // Overlay covers the whole editor so clicking outside the popup dismisses it
    cardPickerOverlay->setBounds (getLocalBounds());
    addAndMakeVisible (*cardPickerOverlay);

    // Compute centred popup rect (280 x 300) within the editor
    const float popW = 280.0f;
    const float popH = 300.0f;
    cardPickerOverlay->popupRect = juce::Rectangle<float> ((getWidth() - popW) * 0.5f,
                                                           (getHeight() - popH) * 0.5f,
                                                           popW, popH);

    cardPickerOverlay->onCardChosen = [this, s] (CardSchema::CardType chosen)
    {
        removeChildComponent (cardPickerOverlay.get());
        cardPickerOverlay.reset();
        setParameterNormalised (CardSchema::cardTypeParam (s),
                                (float)chosen / (float)(CardSchema::numCardTypes - 1));
        // Ensure newly-added card is not bypassed
        setParameterNormalised (CardSchema::bypassParam (s), 0.0f);
    };

    cardPickerOverlay->onDismiss = [this]
    {
        removeChildComponent (cardPickerOverlay.get());
        cardPickerOverlay.reset();
    };

    cardPickerOverlay->toFront (true);
}

//  Randomizer 
void CognitoniBlkFxAudioProcessorEditor::randomizeCards()
{
    std::random_device rd;
    std::mt19937 rng (rd());

    // Randomly decide how many slots get a card (13)
    std::uniform_int_distribution<int> countDist (1, CardSchema::numSlots);
    const int numToFill = countDist (rng);

    // Shuffle slot order so all permutations are equally likely
    std::array<int, CardSchema::numSlots> order { 0, 1, 2 };
    std::shuffle (order.begin(), order.end(), rng);

    std::uniform_int_distribution<int> typeDist (1, CardSchema::numCardTypes - 1);   // 1..4
    std::uniform_real_distribution<float> valDist (0.2f, 0.85f);

    for (int i = 0; i < CardSchema::numSlots; ++i)
    {
        const int slot = order[i];
        if (i < numToFill)
        {
            const float typeNorm = (float)typeDist (rng) / (float)(CardSchema::numCardTypes - 1);
            setParameterNormalised (CardSchema::cardTypeParam (slot), typeNorm);
            setParameterNormalised (CardSchema::amountParam   (slot), valDist (rng));
            setParameterNormalised (CardSchema::wetDryParam   (slot), valDist (rng));
            // Ensure randomized cards are not bypassed
            setParameterNormalised (CardSchema::bypassParam   (slot), 0.0f);
        }
        else
        {
            // Clear this slot
            setParameterNormalised (CardSchema::cardTypeParam (slot), 0.0f);
        }
    }
}

//  Preset dialogs 
void CognitoniBlkFxAudioProcessorEditor::showSavePresetDialog()
{
    auto dialog = std::make_shared<juce::AlertWindow> ("Save Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);
    dialog->setLookAndFeel (&lookAndFeel);
    dialog->addTextEditor ("name", "My Preset");
    dialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, dialog] (int result)
        {
            if (result == 1)
            {
                const auto name = dialog->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                {
                    audioProcessor.saveCurrentPresetAs (name);
                    refreshPresetSelectorItems();
                }
            }
        }), false);
}

void CognitoniBlkFxAudioProcessorEditor::showDeletePresetDialog()
{
    const int idx = audioProcessor.getCurrentPresetIndex();
    if (! audioProcessor.isPresetUserDeletable (idx))
    {
        auto warn = std::make_shared<juce::AlertWindow> ("Cannot Delete", "Built-in presets cannot be deleted.", juce::MessageBoxIconType::NoIcon);
        warn->setLookAndFeel (&lookAndFeel);
        warn->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        warn->enterModalState (true, juce::ModalCallbackFunction::create ([warn](int) {}), false);
        return;
    }
    const auto names = audioProcessor.getPresetNames();
    const auto name  = (idx >= 0 && idx < names.size()) ? names[idx] : "this preset";
    auto dialog = std::make_shared<juce::AlertWindow> ("Delete Preset",
        "Delete \"" + name + "\"?", juce::MessageBoxIconType::NoIcon);
    dialog->setLookAndFeel (&lookAndFeel);
    dialog->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, dialog, idx] (int result)
        {
            if (result == 1)
            {
                audioProcessor.deletePresetByIndex (idx);
                refreshPresetSelectorItems();
            }
        }), false);
}

