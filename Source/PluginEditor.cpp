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
constexpr auto paramInputGain    = "inputGain";
constexpr auto paramOutputGain   = "outputGain";

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
                                                                   juce::Colour accentColour,
                                                                   CardIcon icon)
    : accent (accentColour), cardIcon (icon)
{
    addAndMakeVisible (title);
    title.setText (titleText, juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centredLeft);
    title.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (12.5f).withStyle ("SemiBold"));
    title.setColour (juce::Label::textColourId, juce::Colour::fromRGB (38, 36, 33));
    title.setText (titleText.toUpperCase(), juce::dontSendNotification);

    addAndMakeVisible (bypassButton);
    bypassButton.setButtonText (" ");
    // Store accent on the tick slot so drawToggleButton can retrieve it
    bypassButton.setColour (juce::ToggleButton::tickColourId, accent);

    addAndMakeVisible (amountKnob);
    amountKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);

    addAndMakeVisible (amountLabel);
    amountLabel.setText ("dB", juce::dontSendNotification);
    amountLabel.setJustificationType (juce::Justification::centred);
    amountLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.5f));
    amountLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (120, 115, 110));

    addAndMakeVisible (harmonicType);
    harmonicType.addItem ("Odd", 1);
    harmonicType.addItem ("Even", 2);
    harmonicType.addItem ("Both", 3);
    harmonicType.addItem ("Between", 4);

    addAndMakeVisible (wetDryKnob);
    wetDryKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);

    addAndMakeVisible (wetDryLabel);
    wetDryLabel.setText ("Value", juce::dontSendNotification);
    wetDryLabel.setJustificationType (juce::Justification::centred);
    wetDryLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.5f));
    wetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (120, 115, 110));

    addAndMakeVisible (frequencyRangeSlider);
    // Range slider thumb / fill follows card accent
    frequencyRangeSlider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    frequencyRangeSlider.setColour (juce::Slider::thumbColourId,            accent);

    addAndMakeVisible (frequencyALabel);
    addAndMakeVisible (frequencyBLabel);
    frequencyALabel.setText ("Start", juce::dontSendNotification);
    frequencyBLabel.setText ("End", juce::dontSendNotification);
    frequencyALabel.setJustificationType (juce::Justification::centredLeft);
    frequencyBLabel.setJustificationType (juce::Justification::centredRight);
    frequencyALabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f));
    frequencyBLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f));
    frequencyALabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));
    frequencyBLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 98, 95));
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::paint (juce::Graphics& g)
{
    const float sf = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);
    auto bounds = getLocalBounds().toFloat();
    const float r = 12.0f;

    // Card body — very light warm cream
    g.setColour (juce::Colour::fromRGB (251, 249, 246));
    g.fillRoundedRectangle (bounds, r);

    // Subtle border
    g.setColour (juce::Colour::fromRGB (215, 210, 204));
    g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);

    // Thin separator line below title row (reference card style)
    const float titleRowH = std::round (28.0f * sf);
    const float titleRowY = std::round (6.0f * sf);
    const float sepY      = bounds.getY() + titleRowY + titleRowH + 1.0f;
    const float sepInset  = std::round (14.0f * sf);
    g.setColour (juce::Colour::fromRGB (225, 220, 214));
    g.drawLine (bounds.getX() + sepInset, sepY, bounds.getRight() - sepInset, sepY, 1.0f);

    // Icon area background — dotted/subtle tinted panel
    const float iconY    = std::round (28.0f * sf) + std::round (6.0f * sf);
    const float iconH    = std::round (80.0f * sf);
    const float iconPadX = std::round (16.0f * sf);
    auto iconRect = juce::Rectangle<float> (bounds.getX() + iconPadX, bounds.getY() + iconY,
                                            bounds.getWidth() - iconPadX * 2.0f, iconH);

    g.setColour (accent.withAlpha (0.06f));
    g.fillRoundedRectangle (iconRect, 8.0f);

    // Dot pattern inside icon area
    g.setColour (accent.withAlpha (0.12f));
    const float dotSpacing = juce::jmax (8.0f, 10.0f * sf);
    for (float dx = iconRect.getX() + dotSpacing; dx < iconRect.getRight() - 2.0f; dx += dotSpacing)
        for (float dy = iconRect.getY() + dotSpacing * 0.6f; dy < iconRect.getBottom() - 2.0f; dy += dotSpacing)
            g.fillEllipse (dx - 1.0f, dy - 1.0f, 2.0f, 2.0f);

    // Card icon (stroke, accent color)
    const float iconInset = juce::jmax (10.0f, 14.0f * sf);
    auto drawRect = iconRect.reduced (iconInset, iconInset * 0.6f);
    switch (cardIcon)
    {
        case CardIcon::autoHarm:  drawAutoHarmIcon (g, drawRect, accent); break;
        case CardIcon::contrast:  drawContrastIcon (g, drawRect, accent); break;
        case CardIcon::saws:      drawSawsIcon     (g, drawRect, accent); break;
    }
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
    const float sf   = juce::jlimit (0.55f, 2.2f, (float)getHeight() / 400.0f);

    const int W      = getWidth();
    const int padX   = juce::roundToInt (13.0f * sf);
    const int bypSz  = juce::roundToInt (20.0f * sf);

    // Update fonts proportionally on each resize
    title.setFont          (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (10.0f, 13.0f * sf)));
    amountLabel.setFont    (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (10.0f, 11.5f * sf)));
    wetDryLabel.setFont    (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (10.0f, 11.5f * sf)));
    frequencyALabel.setFont(juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)));
    frequencyBLabel.setFont(juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)));

    // ── Title row: title label + bypass button ─────────────────────────
    const int titleRowH = juce::roundToInt (28.0f * sf);
    const int titleX    = padX;
    const int bypX      = W - padX - bypSz;
    const int titleRowY = juce::roundToInt (6.0f * sf);
    bypassButton.setBounds (bypX, titleRowY + (titleRowH - bypSz) / 2, bypSz, bypSz);
    title.setBounds (titleX, titleRowY, bypX - titleX - 4, titleRowH);
    // Also keep title uppercase when font rescales
    title.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (10.0f, 13.0f * sf)));

    // ── Icon panel ───────────────────────────────────────────
    int y       = titleRowY + titleRowH + juce::roundToInt (4.0f * sf);
    y          += juce::roundToInt (80.0f * sf);    // icon area (drawn in paint)
    y          += juce::roundToInt (8.0f  * sf);    // gap after icon
    const int cW = W - 2 * padX;

    // Large dB knob
    const int bigKnob = juce::roundToInt (78.0f * sf);
    amountKnob.setBounds (padX + (cW - bigKnob) / 2, y, bigKnob, bigKnob);
    y += bigKnob + 2;
    amountLabel.setBounds (padX, y, cW, juce::roundToInt (13.0f * sf));
    y += juce::roundToInt (13.0f * sf) + juce::roundToInt (8.0f * sf);

    // Harmonic type selector (optional)
    if (showHarmonicSelector)
    {
        harmonicType.setBounds (padX, y, cW, 22);
        y += 22 + 6;
    }
    else
    {
        harmonicType.setBounds ({});
    }

    // Small Value knob
    const int smKnob = juce::roundToInt (64.0f * sf);
    wetDryKnob.setBounds (padX + (cW - smKnob) / 2, y, smKnob, smKnob);
    y += smKnob + 2;
    wetDryLabel.setBounds (padX, y, cW, juce::roundToInt (13.0f * sf));
    y += juce::roundToInt (13.0f * sf) + juce::roundToInt (10.0f * sf);

    // Freq range slider
    frequencyRangeSlider.setBounds (padX, y, cW, juce::roundToInt (14.0f * sf));
    y += juce::roundToInt (14.0f * sf) + 2;

    auto labRow = juce::Rectangle<int> (padX, y, cW, juce::roundToInt (13.0f * sf));
    frequencyALabel.setBounds (labRow.removeFromLeft (labRow.getWidth() / 2));
    frequencyBLabel.setBounds (labRow);

    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// Card icon drawing — all paths normalised to their bounding rect
// ─────────────────────────────────────────────────────────────────────────────
void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawAutoHarmIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    const float w = b.getWidth(), h = b.getHeight();
    auto pt = [&] (float nx, float ny) { return juce::Point<float> (b.getX() + nx * w, b.getY() + ny * h); };

    // Filled back silhouette
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

    // Front spectrum curve with filled area below
    juce::Path front;
    front.startNewSubPath (pt (0.0f, 1.0f));
    front.lineTo (pt (0.08f, 0.60f));
    front.lineTo (pt (0.16f, 0.40f));
    front.lineTo (pt (0.22f, 0.50f));
    front.lineTo (pt (0.30f, 0.22f));
    front.lineTo (pt (0.36f, 0.07f));
    front.lineTo (pt (0.42f, 0.18f));
    front.lineTo (pt (0.50f, 0.30f));
    front.lineTo (pt (0.56f, 0.14f));
    front.lineTo (pt (0.64f, 0.28f));
    front.lineTo (pt (0.70f, 0.42f));
    front.lineTo (pt (0.76f, 0.32f));
    front.lineTo (pt (0.83f, 0.52f));
    front.lineTo (pt (0.90f, 0.46f));
    front.lineTo (pt (0.96f, 0.60f));
    front.lineTo (pt (1.0f,  1.0f));
    front.closeSubPath();
    // gradient fill under the curve
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.30f), b.getTopLeft(),
                                             col.withAlpha (0.04f), b.getBottomLeft(), false));
    g.fillPath (front);
    // stroke on top
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

    // Peak highlight dots at the top two peaks
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
    // 8-point starburst: alternating long and short rays from centre
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
        const auto  p = juce::Point<float> (cx + std::cos (angle) * r,
                                            cy + std::sin (angle) * r);
        if (i == 0) star.startNewSubPath (p);
        else        star.lineTo (p);
    }
    star.closeSubPath();

    // Semi-transparent fill
    g.setColour (col.withAlpha (0.13f));
    g.fillPath (star);

    // Outline
    g.setColour (col.withAlpha (0.85f));
    g.strokePath (star, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered,
                                              juce::PathStrokeType::square));

    // Inner circle accent
    const float innerR = rShort * 0.65f;
    g.setColour (col.withAlpha (0.30f));
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 1.2f);
    g.setColour (col.withAlpha (0.18f));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);

    // Centre dot
    g.setColour (col);
    g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

void CognitoniBlkFxAudioProcessorEditor::CardComponent::drawSawsIcon (
    juce::Graphics& g, juce::Rectangle<float> b, juce::Colour col)
{
    const float w = b.getWidth(), h = b.getHeight();
    auto pt = [&] (float nx, float ny) { return juce::Point<float> (b.getX() + nx * w, b.getY() + ny * h); };

    const juce::PathStrokeType stroke (2.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::square);
    const juce::PathStrokeType thinStroke (1.0f);

    // Baseline (dashed — drawn as short segments)
    g.setColour (col.withAlpha (0.25f));
    const float baseY = 0.92f;
    const float segW  = 0.05f;
    for (float sx = 0.0f; sx < 1.0f; sx += segW * 2.0f)
        g.drawLine (juce::Line<float> (pt (sx, baseY), pt (juce::jmin (sx + segW, 1.0f), baseY)), 1.0f);

    // Helper to draw one filled sawtooth wave and its stroke
    auto drawSaw = [&] (float x0, float x1, int cycles)
    {
        const float cycleW = (x1 - x0) / cycles;
        juce::Path fill;
        fill.startNewSubPath (pt (x0, baseY));
        for (int c = 0; c < cycles; ++c)
        {
            const float xa = x0 + c * cycleW;
            const float xb = xa + cycleW;
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
            const float xa = x0 + c * cycleW;
            const float xb = xa + cycleW;
            saw.lineTo (pt (xb - 0.001f, 0.08f));
            saw.lineTo (pt (xb - 0.001f, baseY));
        }
        g.setColour (col.withAlpha (0.88f));
        g.strokePath (saw, stroke);
    };

    drawSaw (0.02f, 0.44f, 2);
    drawSaw (0.56f, 0.98f, 2);
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
    // ── Cream / warm-white palette (Lifeline-inspired) ─────────────────────
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
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (5.0f);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto radius = side * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = juce::jmap (sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

    const auto accentCol = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // ── Arc track (thin, light warm-gray) ─────────────────────────────────
    const auto trackR = radius - 5.0f;
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour::fromRGB (190, 185, 178));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // ── Value arc (accent colour, slightly thicker) ────────────────────────
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f,
                           rotaryStartAngle, angle, true);
        g.setColour (accentCol);
        g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // ── Knob cap: subtle drop-shadow then gradient disc ────────────────────
    const auto capR = radius - 10.0f;
    auto capBounds  = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (capBounds.expanded (2.0f));

    // Gradient: lighter top-left → slightly darker bottom-right
    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (242, 238, 233),
                                             centre.translated (-capR * 0.3f, -capR * 0.3f),
                                             juce::Colour::fromRGB (218, 214, 208),
                                             centre.translated ( capR * 0.3f,  capR * 0.3f),
                                             false));
    g.fillEllipse (capBounds);

    g.setColour (juce::Colour::fromRGB (192, 188, 182));
    g.drawEllipse (capBounds.reduced (0.5f), 1.0f);

    // ── Indicator dot on cap face ──────────────────────────────────────────
    const auto dotDist = capR - 5.0f;
    const auto dotPt   = centre + juce::Point<float> (std::cos (angle - juce::MathConstants<float>::halfPi) * dotDist,
                                                      std::sin (angle - juce::MathConstants<float>::halfPi) * dotDist);
    g.setColour (accentCol);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (dotPt));
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

    if (style != juce::Slider::TwoValueHorizontal && style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        const auto centreX  = static_cast<float> (x + width / 2);
        const auto trackTop    = static_cast<float> (y + 8);
        const auto trackBottom = static_cast<float> (y + height - 8);
        const auto trackW   = 5.0f;
        const auto thumbH   = 18.0f;
        const auto thumbW   = juce::jmin (static_cast<float> (width) - 4.0f, 38.0f);

        // Track background
        g.setColour (juce::Colour::fromRGB (58, 66, 78));
        g.fillRoundedRectangle (centreX - trackW * 0.5f, trackTop, trackW, trackBottom - trackTop, trackW * 0.5f);

        // Track fill (from sliderPos to bottom = filled portion)
        g.setColour (juce::Colour::fromRGB (100, 160, 220).withAlpha (0.8f));
        g.fillRoundedRectangle (centreX - trackW * 0.5f, sliderPos, trackW, trackBottom - sliderPos, trackW * 0.5f);

        // Thumb
        const auto thumbY = sliderPos - thumbH * 0.5f;
        auto thumbBounds = juce::Rectangle<float> (centreX - thumbW * 0.5f, thumbY, thumbW, thumbH);
        g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (228, 224, 218), thumbBounds.getTopLeft(),
                                                 juce::Colour::fromRGB (205, 200, 193), thumbBounds.getBottomLeft(), false));
        g.fillRoundedRectangle (thumbBounds, 4.0f);
        g.setColour (juce::Colour::fromRGB (178, 175, 169));
        g.drawRoundedRectangle (thumbBounds.reduced (0.5f), 4.0f, 1.0f);

        // Centre grip line on thumb
        const float gx = thumbBounds.getCentreX();
        const float gy = thumbBounds.getCentreY();
        for (int i = -1; i <= 1; ++i)
        {
            const float lineY = gy + static_cast<float> (i) * 4.5f;
            g.setColour (juce::Colour::fromRGB (148, 144, 138));
            g.drawLine (gx - 8.0f, lineY, gx + 8.0f, lineY, 1.0f);
        }
        return;
    }

    const auto centreY = static_cast<float> (y + (height / 2));
    const auto left    = static_cast<float> (x + 8);
    const auto right   = static_cast<float> (x + width - 8);

    // Track
    g.setColour (juce::Colour::fromRGB (185, 180, 174));
    g.drawLine (left, centreY, right, centreY, 4.0f);

    // Fill
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.drawLine (minSliderPos, centreY, maxSliderPos, centreY, 4.0f);

    // Thumbs
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
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));       // cream
    g.fillRoundedRectangle (bounds, 7.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    // Chevron
    const auto cx = static_cast<float> (buttonX + buttonW / 2);
    const auto cy = static_cast<float> (buttonY + buttonH / 2);
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx,       cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (juce::Colour::fromRGB (95, 100, 110));
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CognitoniBlkFxAudioProcessorEditor::CognitoniLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                                                  juce::ToggleButton& button,
                                                                                  bool shouldDrawButtonAsHighlighted,
                                                                                  bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto bounds = button.getLocalBounds().toFloat().reduced (1.5f);
    const bool active = ! button.getToggleState();   // toggleState = bypassed

    // Retrieve the per-card accent colour stored in tickColourId
    const auto accentCol = button.findColour (juce::ToggleButton::tickColourId);

    // Ring: warm-dark border on light card background
    g.setColour (juce::Colour::fromRGB (145, 140, 135));
    g.drawEllipse (bounds.reduced (0.5f), 1.5f);

    // Fill: accent colour when active, muted grey when bypassed
    auto inner = bounds.reduced (3.5f);
    g.setColour (active ? accentCol : juce::Colour::fromRGB (205, 200, 195));
    g.fillEllipse (inner);
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

    // ── Plugin name label (top-left header) ───────────────────────────────
    pluginNameLabel.setText ("blkfx", juce::dontSendNotification);
    pluginNameLabel.setJustificationType (juce::Justification::centredLeft);
    pluginNameLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (28.0f).withStyle ("Bold"));
    pluginNameLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (38, 42, 50));
    addAndMakeVisible (pluginNameLabel);

    presetLabel.setText ("Preset:", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f));
    presetLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (100, 96, 92));
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

    versionLabel.setButtonText ("v0.1.0alpha");
    versionLabel.setURL (juce::URL ("https://github.com/toni-lyttinen/CognitoniBlkFx/tags"));
    versionLabel.setFont (juce::Font (juce::FontOptions().withName ("Segoe UI").withHeight (10.0f)), false, juce::Justification::centred);
    versionLabel.setColour (juce::HyperlinkButton::textColourId, juce::Colour::fromRGB (148, 144, 138));
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

    // ── Level meters (right panel) ────────────────────────────────────────
    addAndMakeVisible (inputLevelMeter);
    addAndMakeVisible (outputLevelMeter);

    inputMeterLabel.setText ("IN", juce::dontSendNotification);
    inputMeterLabel.setJustificationType (juce::Justification::centred);
    inputMeterLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f).withStyle ("Bold"));
    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 168, 162));
    addAndMakeVisible (inputMeterLabel);

    outputMeterLabel.setText ("OUT", juce::dontSendNotification);
    outputMeterLabel.setJustificationType (juce::Justification::centred);
    outputMeterLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f).withStyle ("Bold"));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 168, 162));
    addAndMakeVisible (outputMeterLabel);

    // ── Input / Output gain knobs (right panel) ────────────────────────────
    auto setupGainKnob = [this] (juce::Slider& knob)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
        knob.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour::fromRGB (135, 175, 215));
        knob.setColour (juce::Slider::textBoxTextColourId,         juce::Colour::fromRGB (175, 172, 165));
        knob.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        knob.setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        addAndMakeVisible (knob);
    };
    setupGainKnob (inputGainKnob);
    setupGainKnob (outputGainKnob);
    // Match card-knob drag axis
    inputGainKnob.setSliderStyle  (juce::Slider::RotaryVerticalDrag);
    outputGainKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    inputGainKnob.textFromValueFunction  = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    outputGainKnob.textFromValueFunction = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    inputGainKnob.setTooltip  ("Input gain: -18 to +18 dB");
    outputGainKnob.setTooltip ("Output gain: -18 to +18 dB");

    // Title colour is dark (set in CardComponent ctor); no override needed here.

    configureAmountKnob (autoHarmCard.amountKnob);
    configureAmountKnob (contrastCard.amountKnob);
    configureAmountKnob (sawsCard.amountKnob);
    configureAmountKnob (autoHarmCard.wetDryKnob);
    configureAmountKnob (contrastCard.wetDryKnob);
    configureAmountKnob (sawsCard.wetDryKnob);
    configureAmountKnob (masterWetDryKnob);
    masterWetDryKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterWetDryKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
    masterWetDryKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB (135, 175, 215));
    masterWetDryKnob.setColour (juce::Slider::textBoxTextColourId,       juce::Colour::fromRGB (175, 172, 165));
    masterWetDryKnob.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    masterWetDryKnob.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    configureRangeSlider (autoHarmCard.frequencyRangeSlider);
    configureRangeSlider (contrastCard.frequencyRangeSlider);
    configureRangeSlider (sawsCard.frequencyRangeSlider);

    masterWetDryLabel.setText ("Mix", juce::dontSendNotification);
    masterWetDryLabel.setJustificationType (juce::Justification::centred);
    masterWetDryLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (11.0f).withStyle ("Bold"));
    masterWetDryLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (170, 168, 162));

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
    inputGainAttachment  = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramInputGain,  inputGainKnob);
    outputGainAttachment = std::make_unique<SliderAttachment> (audioProcessor.getAPVTS(), paramOutputGain, outputGainKnob);

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
    setResizeLimits (640, 640, 1200, 900);
    setSize (760, 660);
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
    // ── Outer background ─────────────────────────────────────────────────
    g.fillAll (juce::Colour::fromRGB (205, 200, 193));

    // ── Main panel (warm cream gradient) ─────────────────────────────────
    auto panel = getLocalBounds().toFloat().reduced (12.0f);

    // subtle drop shadow
    g.setColour (juce::Colour::fromRGBA (100, 96, 90, 55));
    g.fillRoundedRectangle (panel.translated (0.0f, 5.0f), 18.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (249, 245, 240), panel.getTopLeft(),
                                             juce::Colour::fromRGB (240, 236, 230), panel.getBottomLeft(), false));
    g.fillRoundedRectangle (panel, 18.0f);

    g.setColour (juce::Colour::fromRGB (225, 220, 213).withAlpha (0.8f));
    g.drawRoundedRectangle (panel.reduced (0.75f), 18.0f, 1.5f);

    // ── Right side-panel (dark) ───────────────────────────────────────────
    const int sidePanelW  = 148;
    const int sidePanelMargin = 18;
    const auto rightPanelX = getWidth() - sidePanelMargin - sidePanelW;
    const auto rightPanelY = sidePanelMargin;
    const auto rightPanelH = getHeight() - sidePanelMargin * 2;

    auto rPanel = juce::Rectangle<float> (static_cast<float> (rightPanelX),
                                          static_cast<float> (rightPanelY),
                                          static_cast<float> (sidePanelW),
                                          static_cast<float> (rightPanelH));
    g.setColour (juce::Colour::fromRGB (42, 48, 56));
    g.fillRoundedRectangle (rPanel, 14.0f);

    g.setColour (juce::Colour::fromRGB (58, 65, 75));
    g.drawRoundedRectangle (rPanel.reduced (0.5f), 14.0f, 1.0f);
}

void CognitoniBlkFxAudioProcessorEditor::resized()
{
    // Scale text proportionally with window height (base design: 520 px).
    const float sf = juce::jlimit (0.55f, 2.0f, (float)getHeight() / 520.0f);
    pluginNameLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (16.0f, 28.0f * sf)).withStyle ("Bold"));
    presetLabel.setFont     (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)));
    versionLabel.setFont    (juce::Font (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax (8.5f, 10.5f * sf))), false, juce::Justification::centred);
    inputMeterLabel.setFont  (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)).withStyle ("Bold"));
    outputMeterLabel.setFont (juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)).withStyle ("Bold"));
    masterWetDryLabel.setFont(juce::FontOptions().withName ("Segoe UI").withHeight (juce::jmax  (9.0f, 11.0f * sf)).withStyle ("Bold"));

    const int margin       = 22;
    const int sidePanelW   = 148;
    const int sidePanelGap = 14;    // gap between cards area and right panel

    // Split into left content + right panel
    auto full = getLocalBounds().reduced (margin);

    // ── Footer (version label is inside right panel — see rp layout)
    auto footer = full.removeFromBottom (16);
    debugInfoLabel.setBounds (footer.removeFromLeft (juce::jmin (360, footer.getWidth())));

    // ── Right dark panel ────────────────────────────────────────────────────
    full.removeFromRight (0);   // padding already handled in paint
    auto rightPanel = full.removeFromRight (sidePanelW);
    full.removeFromRight (sidePanelGap);

    // ── Header row (plugin name + preset controls) ─────────────────────────
    auto header = full.removeFromTop (50);
    header.removeFromBottom (6);

    pluginNameLabel.setBounds (header.removeFromLeft (130));
    header.removeFromLeft (16);

    const auto iconSz = 26;
    const auto rowGap = 6;
    auto presetRow   = header;

    // Right-align preset inside header
    deletePresetButton.setBounds (presetRow.removeFromRight (iconSz).withSizeKeepingCentre (iconSz, iconSz));
    presetRow.removeFromRight (rowGap);
    savePresetButton.setBounds (presetRow.removeFromRight (iconSz).withSizeKeepingCentre (iconSz, iconSz));
    presetRow.removeFromRight (rowGap);

    const auto desiredBoxWidth = 160;
    auto selectorArea = presetRow.removeFromRight (desiredBoxWidth);
    presetSelector.setBounds (selectorArea.withSizeKeepingCentre (desiredBoxWidth, 24));
    presetLabel.setBounds (presetRow.removeFromRight (52).withSizeKeepingCentre (52, 16));

    full.removeFromTop (6);   // gap between header and cards

    // ── 3 card columns ────────────────────────────────────────────────────
    const auto cardGap   = 12;
    const auto cardWidth = (full.getWidth() - cardGap * 2) / 3;

    auto cardsArea = full;
    autoHarmCard.setBounds (cardsArea.removeFromLeft (cardWidth));
    cardsArea.removeFromLeft (cardGap);
    contrastCard.setBounds (cardsArea.removeFromLeft (cardWidth));
    cardsArea.removeFromLeft (cardGap);
    sawsCard.setBounds (cardsArea.removeFromLeft (cardWidth));

    // ── Right panel internals ──────────────────────────────────────────────
    auto rp = rightPanel.reduced (12, 14);

    const int colW   = (rp.getWidth() - 8) / 2; // width per column (IN / OUT)
    const int labelH = 12;

    // Column headers: "IN" over left, "OUT" over right
    auto topLabels = rp.removeFromTop (labelH);
    inputMeterLabel.setBounds  (topLabels.removeFromLeft (colW));
    topLabels.removeFromLeft (8);
    outputMeterLabel.setBounds (topLabels.removeFromLeft (colW));
    rp.removeFromTop (3);

    // Gain knobs under column headers
    const int gainKnobH = 66;  // rotary 52px + textbox 14px
    auto gainRow = rp.removeFromTop (gainKnobH);
    inputGainKnob.setBounds  (gainRow.removeFromLeft (colW));
    gainRow.removeFromLeft (8);
    outputGainKnob.setBounds (gainRow.removeFromLeft (colW));
    rp.removeFromTop (6);

    // Level meters: take remaining height minus mix area
    const int mixKnobSz  = juce::jmin (rp.getWidth() - 8, 52);
    const int mixAreaH   = labelH + 4 + mixKnobSz + 16;  // label + gap + rotary + textbox
    const int meterH     = rp.getHeight() - mixAreaH - 10;
    auto metersArea = rp.removeFromTop (juce::jmax (30, meterH));
    inputLevelMeter.setBounds  (metersArea.removeFromLeft (colW));
    metersArea.removeFromLeft (8);
    outputLevelMeter.setBounds (metersArea.removeFromLeft (colW));

    rp.removeFromTop (10);

    // Mix label + rotary knob
    masterWetDryLabel.setBounds (rp.removeFromTop (labelH));
    rp.removeFromTop (4);
    const int mixKnobH = mixKnobSz + 16;   // knob + textbox
    auto mixArea = rp.removeFromTop (mixKnobH);
    masterWetDryKnob.setBounds (mixArea.withSizeKeepingCentre (mixKnobSz + 16, mixKnobH));

    // Version label centred at the bottom of the right panel
    versionLabel.setBounds (rightPanel.removeFromBottom (16).reduced (4, 2));

    repaint();
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
    inputGainKnob.updateText();
    outputGainKnob.updateText();

    // ── Level meters ──────────────────────────────────────────────────────
    inputLevelMeter.setLevel  (audioProcessor.getLastInputRms());
    inputLevelMeter.repaint();
    outputLevelMeter.setLevel (audioProcessor.getLastOutputRms());
    outputLevelMeter.repaint();

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
