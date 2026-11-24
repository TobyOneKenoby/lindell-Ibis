#include "PluginEditor.h"

using namespace heron::ui;
namespace col = heron::ui::colours;

//==============================================================================
// Layout (in the 1000 x 560 base coordinate space; the editor scales it)
//==============================================================================
namespace layout
{
    constexpr int headerH = 52;
    const juce::Rectangle<int> curve { 14, 58, 972, 182 };
    constexpr int plateTop = 252, plateBottom = 548;
    const juce::Rectangle<int> col0 { 14, plateTop, 180, plateBottom - plateTop };
    inline juce::Rectangle<int> bandCol (int i) { return { 204 + i * 196, plateTop, 190, plateBottom - plateTop }; }
}

static bool sameSettings (const heron::Settings& a, const heron::Settings& b)
{
    for (size_t i = 0; i < 4; ++i)
    {
        const auto &x = a.bands[i], &y = b.bands[i];
        if (x.position != y.position || x.plusStep != y.plusStep || x.gainDb != y.gainDb
            || x.bandwidthOct != y.bandwidthOct || x.shelf != y.shelf)
            return false;
    }
    return a.lowCutIndex == b.lowCutIndex && a.lowCutSteep == b.lowCutSteep
        && a.colourSource == b.colourSource && a.colour == b.colour;
}

static juce::String hzText (double hz)
{
    if (hz >= 1000.0) return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
    return (hz < 100.0 ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz))) + " Hz";
}

//==============================================================================
// Look and feel
//==============================================================================
HeronLookAndFeel::HeronLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::copper.withAlpha (0.4f));
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::groove);
    setColour (juce::CaretComponent::caretColourId, col::copper);
}

juce::Font HeronLookAndFeel::getLabelFont (juce::Label& l)
{
    return juce::Font (juce::FontOptions (l.getFont().getHeight()));
}

juce::Label* HeronLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::Font (juce::FontOptions (12.0f)));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, col::textDim.brighter (0.3f));
    l->setColour (juce::Label::outlineWhenEditingColourId, col::copper);
    return l;
}

void HeronLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    auto* knob = dynamic_cast<Knob*> (&slider);
    const auto role = knob ? knob->role : Knob::Role::Utility;
    const int steps = knob ? knob->steps : 0;

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 7.0f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Scale ticks
    const int numTicks = steps > 0 ? steps : 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = numTicks > 1 ? (float) i / (float) (numTicks - 1) : 0.0f;
        const float a = startAngle + t * (endAngle - startAngle);
        const bool major = steps > 0 || i % 5 == 0;
        const float r0 = radius + 3.0f, r1 = radius + (major ? 7.0f : 5.0f);
        g.setColour (col::textDim.withAlpha (major ? 0.9f : 0.5f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a),
                    c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), major ? 1.4f : 1.0f);
    }

    juce::Colour cap = col::slate;
    if (role == Knob::Role::Gain) cap = col::copper;
    else if (role == Knob::Role::Bandwidth) cap = col::bone;
    else if (role == Knob::Role::Utility) cap = juce::Colour (0xff8c8f93);
    if (! slider.isEnabled()) cap = cap.withSaturation (0.0f).darker (0.4f);

    // Value arc (gain arcs out from the centre detent)
    if (steps == 0)
    {
        const float from = role == Knob::Role::Gain ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius + 1.0f, radius + 1.0f, 0.0f, from, angle, true);
        g.setColour (cap.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - radius + 1.5f, c.y - radius + 3.0f, radius * 2.0f, radius * 2.0f);

    // Knurled skirt
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d41), c.x, c.y - radius,
                                             juce::Colour (0xff141517), c.x, c.y + radius, false));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    const int knurls = 36;
    for (int i = 0; i < knurls; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) knurls;
        g.drawLine (c.x + radius * 0.86f * std::sin (a), c.y - radius * 0.86f * std::cos (a),
                    c.x + radius * 0.99f * std::sin (a), c.y - radius * 0.99f * std::cos (a), 1.0f);
    }

    // Cap
    const float rc = radius * 0.74f;
    g.setGradientFill (juce::ColourGradient (cap.brighter (0.35f), c.x - rc * 0.5f, c.y - rc,
                                             cap.darker (0.7f), c.x + rc * 0.4f, c.y + rc, true));
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f, 1.0f);

    // Pointer
    g.setColour (col::text);
    const float p0 = rc * 0.25f, p1 = radius * 0.96f;
    g.drawLine (c.x + p0 * std::sin (angle), c.y - p0 * std::cos (angle),
                c.x + p1 * std::sin (angle), c.y - p1 * std::cos (angle), 2.4f);
}

void HeronLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    auto* led = dynamic_cast<LedButton*> (&b);
    const auto ledCol = led ? led->ledColour : col::copper;

    g.setColour (down ? col::groove : (highlighted ? col::panelLight.brighter (0.08f) : col::panelLight));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const float d = juce::jmin (r.getHeight() * 0.42f, 9.0f);
    const juce::Rectangle<float> ledR (r.getX() + 7.0f, r.getCentreY() - d * 0.5f, d, d);
    if (on)
    {
        g.setColour (ledCol.withAlpha (0.25f));
        g.fillEllipse (ledR.expanded (3.5f));
        g.setColour (ledCol);
    }
    else
    {
        g.setColour (ledCol.withBrightness (0.18f));
    }
    g.fillEllipse (ledR);

    g.setColour (on ? col::text : col::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 10.0f).withTrimmedRight (4.0f), juce::Justification::centred);
}

//==============================================================================
// Response curve
//==============================================================================
void ResponseCurve::setSettings (const heron::Settings& s, bool bypassed)
{
    settings = s; isBypassed = bypassed;
    repaint();
}

void ResponseCurve::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (col::groove);
    g.fillRoundedRectangle (area, 6.0f);

    const auto plot = area.reduced (34.0f, 12.0f).withTrimmedBottom (8.0f);
    const double fMin = 20.0, fMax = 20000.0, dbRange = 15.0;
    auto xFor = [&] (double hz) { return plot.getX() + (float) (std::log (hz / fMin) / std::log (fMax / fMin)) * plot.getWidth(); };
    auto yFor = [&] (double db) { return plot.getCentreY() - (float) (db / dbRange) * plot.getHeight() * 0.5f; };

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (double hz : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                       1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
    {
        const bool major = hz == 20.0 || hz == 100.0 || hz == 1000.0 || hz == 10000.0 || hz == 20000.0;
        g.setColour (juce::Colours::white.withAlpha (major ? 0.10f : 0.04f));
        g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
        if (major || hz == 50.0 || hz == 500.0 || hz == 5000.0)
        {
            g.setColour (col::textDim.withAlpha (0.8f));
            const auto label = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
            g.drawText (label, juce::Rectangle<float> (xFor (hz) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
    }
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.16f : 0.06f));
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (col::textDim.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (area.getX() + 4.0f, yFor (db) - 6.0f, 26.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // Curve
    juce::Path curvePath, fill;
    const int pts = juce::jmax (2, (int) plot.getWidth());
    for (int i = 0; i <= pts; ++i)
    {
        const double hz = fMin * std::pow (fMax / fMin, (double) i / pts);
        const double db = juce::jlimit (-dbRange - 3.0, dbRange + 3.0, 20.0 * std::log10 (juce::jmax (1e-6, heron::magnitudeAt (settings, hz))));
        const float px = plot.getX() + plot.getWidth() * (float) i / (float) pts, py = yFor (db);
        if (i == 0) { curvePath.startNewSubPath (px, py); fill.startNewSubPath (px, yFor (0)); }
        else curvePath.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (plot.getRight(), yFor (0));
    fill.closeSubPath();

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt().expanded (0, 4));
    const auto accent = isBypassed ? col::textDim : col::copper;
    g.setColour (accent.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (accent.withAlpha (isBypassed ? 0.5f : 1.0f));
    g.strokePath (curvePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Band markers
    for (int b = 0; b < 4; ++b)
    {
        const auto& bs = settings.bands[(size_t) b];
        const double hz = heron::freqs::hzFor (b, bs.position, bs.plusStep);
        const double db = 20.0 * std::log10 (heron::magnitudeAt (settings, hz));
        const bool active = heron::applyDeadZone (bs.gainDb) != 0.0;
        g.setColour (col::bandColours[b].withAlpha (active && ! isBypassed ? 1.0f : 0.35f));
        g.fillEllipse (xFor (hz) - 4.5f, yFor (db) - 4.5f, 9.0f, 9.0f);
    }
    g.restoreState();

    if (isBypassed)
    {
        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("BYPASSED", plot.reduced (8.0f), juce::Justification::topRight);
    }
}

//==============================================================================
// Static panel artwork
//==============================================================================
void HeronEQAudioProcessorEditor::Panel::paint (juce::Graphics& g)
{
    // Background: dark anodised panel with a subtle vertical sheen
    g.setGradientFill (juce::ColourGradient (col::panel.brighter (0.06f), 0, 0, col::panel.darker (0.25f), 0, (float) getHeight(), false));
    g.fillAll();

    // Header
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)).withExtraKerningFactor (0.18f));
    g.drawText ("HERON", 22, 10, 160, 34, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)).withExtraKerningFactor (0.12f));
    g.drawText ("DISCRETE-STYLE PARALLEL EQUALISER", 142, 18, 320, 20, juce::Justification::centredLeft);

    // Rack screws for a bit of hardware feel
    for (auto p : { juce::Point<float> (8, 8), juce::Point<float> ((float) getWidth() - 16, 8) })
    {
        g.setColour (juce::Colour (0xff0d0e10)); g.fillEllipse (p.x, p.y, 8, 8);
        g.setColour (juce::Colour (0xff4a4d52)); g.drawEllipse (p.x, p.y, 8, 8, 1.0f);
    }

    auto plate = [&] (juce::Rectangle<int> r)
    {
        g.setColour (col::panelLight);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    };
    auto caption = [&] (const juce::String& t, juce::Rectangle<int> r, juce::Colour c = col::textDim)
    {
        g.setColour (c);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawText (t, r, juce::Justification::centred);
    };

    // Low cut / colour column
    const auto c0 = layout::col0;
    plate (c0);
    caption ("LOW CUT", { c0.getX(), c0.getY() + 8, c0.getWidth(), 14 }, col::text);
    caption ("COLOUR", { c0.getX(), c0.getY() + 146, c0.getWidth(), 14 }, col::text);
    caption ("SOURCE", { c0.getX() + 4, c0.getY() + 166, 84, 12 });
    caption ("AMOUNT", { c0.getX() + 92, c0.getY() + 166, 84, 12 });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawHorizontalLine (c0.getY() + 138, (float) c0.getX() + 10, (float) c0.getRight() - 10);

    const char* bandNames[4] = { "BAND 1  LOW", "BAND 2  LOW-MID", "BAND 3  HIGH-MID", "BAND 4  HIGH" };
    for (int i = 0; i < 4; ++i)
    {
        const auto r = layout::bandCol (i);
        plate (r);
        g.setColour (col::bandColours[i]);
        g.fillRoundedRectangle ((float) r.getX() + 10, (float) r.getY() + 12, 8, 8, 2);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawText (bandNames[i], r.getX() + 24, r.getY() + 8, r.getWidth() - 30, 16, juce::Justification::centredLeft);

        caption ("CUT", { r.getX() + 22, r.getY() + 120, 40, 12 });
        caption ("BOOST", { r.getRight() - 64, r.getY() + 120, 44, 12 });
        caption ("FREQ", { r.getX() + 8, r.getY() + 160, 88, 12 });
        caption ("WIDTH", { r.getX() + 98, r.getY() + 160, 84, 12 });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawHorizontalLine (r.getY() + 152, (float) r.getX() + 10, (float) r.getRight() - 10);
    }
}

//==============================================================================
// Editor
//==============================================================================
HeronEQAudioProcessorEditor::HeronEQAudioProcessorEditor (HeronEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    panel.addAndMakeVisible (curve);

    auto& apvts = proc.apvts;

    for (int i = 0; i < 4; ++i)
    {
        auto& b = bands[(size_t) i];
        b.gain = std::make_unique<Knob> (Knob::Role::Gain);
        b.freq = std::make_unique<Knob> (Knob::Role::Freq, heron::freqs::numPositions);
        b.bw = std::make_unique<Knob> (Knob::Role::Bandwidth);
        b.step = std::make_unique<LedButton> ("+1 STEP", col::ledRed);
        b.freq->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

        for (auto* k : { b.gain.get(), b.freq.get(), b.bw.get() })
            panel.addAndMakeVisible (*k);
        panel.addAndMakeVisible (*b.step);

        b.freqReadout.setJustificationType (juce::Justification::centred);
        b.freqReadout.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        b.freqReadout.setColour (juce::Label::textColourId, col::text);
        b.freqReadout.setInterceptsMouseClicks (false, false);
        panel.addAndMakeVisible (b.freqReadout);

        b.gainAtt = std::make_unique<SliderAtt> (apvts, ids::bandGain (i), *b.gain);
        b.freqAtt = std::make_unique<SliderAtt> (apvts, ids::bandFreq (i), *b.freq);
        b.bwAtt = std::make_unique<SliderAtt> (apvts, ids::bandBw (i), *b.bw);
        b.stepAtt = std::make_unique<ButtonAtt> (apvts, ids::bandStep (i), *b.step);

        b.gain->setDoubleClickReturnValue (true, 0.0);
        b.bw->setDoubleClickReturnValue (true, 1.5);
        b.gain->setTooltip ("Boost / cut, +/-12 dB. There is a flat zone of +/-0.3 dB at the centre.");
        b.bw->setTooltip ("Bandwidth in octaves (at full boost)");

        if (i == 0 || i == 3)
        {
            b.shelf = std::make_unique<LedButton> ("SHELF", col::ledTeal);
            panel.addAndMakeVisible (*b.shelf);
            b.shelfAtt = std::make_unique<ButtonAtt> (apvts, ids::bandShelf (i), *b.shelf);
        }
    }

    for (auto* k : { &lowCut, &colourSource, &colour })
        panel.addAndMakeVisible (*k);
    panel.addAndMakeVisible (steep);
    panel.addAndMakeVisible (inButton);

    lowCutAtt = std::make_unique<SliderAtt> (apvts, ids::lowCut, lowCut);
    colourSourceAtt = std::make_unique<SliderAtt> (apvts, ids::colourSrc, colourSource);
    colourAtt = std::make_unique<SliderAtt> (apvts, ids::colour, colour);
    steepAtt = std::make_unique<ButtonAtt> (apvts, ids::lowCutSteep, steep);
    colour.setDoubleClickReturnValue (true, 0.0);

    // The IN button is the inverse of the host bypass parameter.
    inButton.setClickingTogglesState (true);
    inButton.onClick = [this]
    {
        if (auto* bp = proc.apvts.getParameter (ids::bypass))
        {
            bp->beginChangeGesture();
            bp->setValueNotifyingHost (inButton.getToggleState() ? 0.0f : 1.0f);
            bp->endChangeGesture();
        }
    };

    setResizable (true, true);
    setResizeLimits (baseW * 7 / 10, baseH * 7 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setSize (baseW, baseH);

    timerCallback();
    startTimerHz (30);
}

HeronEQAudioProcessorEditor::~HeronEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HeronEQAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (col::panel); }

void HeronEQAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, baseW, baseH);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseW));
    layoutPanel();
}

void HeronEQAudioProcessorEditor::layoutPanel()
{
    curve.setBounds (layout::curve);
    inButton.setBounds (baseW - 92, 14, 70, 26);

    const auto c0 = layout::col0;
    lowCut.setBounds (c0.getX() + 8, c0.getY() + 26, 96, 104);
    steep.setBounds (c0.getX() + 104, c0.getY() + 62, 68, 24);
    colourSource.setBounds (c0.getX() + 6, c0.getY() + 180, 84, 100);
    colour.setBounds (c0.getX() + 92, c0.getY() + 180, 84, 100);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = bands[(size_t) i];
        const auto r = layout::bandCol (i);
        b.gain->setBounds (r.getX() + 40, r.getY() + 28, 100, 120);
        if (b.shelf) b.shelf->setBounds (r.getRight() - 72, r.getY() + 30, 66, 22);
        b.freq->setBounds (r.getX() + 14, r.getY() + 174, 80, 76);
        b.freqReadout.setBounds (r.getX() + 4, r.getY() + 248, 100, 16);
        b.step->setBounds (r.getX() + 16, r.getY() + 266, 76, 22);
        b.bw->setBounds (r.getX() + 102, r.getY() + 174, 80, 96);
    }
}

void HeronEQAudioProcessorEditor::timerCallback()
{
    const auto s = HeronEQAudioProcessor::readSettings (proc.apvts);
    const bool bypassed = proc.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;

    inButton.setToggleState (! bypassed, juce::dontSendNotification);

    if (first || bypassed != lastBypass || ! sameSettings (s, lastSettings))
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto& bs = s.bands[(size_t) i];
            const int note = heron::freqs::noteFor (i, bs.position, bs.plusStep);
            bands[(size_t) i].freqReadout.setText (juce::String (heron::freqs::noteName (note)) + "  " + hzText (heron::freqs::noteToHz (note)),
                                                   juce::dontSendNotification);
            if (bands[(size_t) i].shelf)
                bands[(size_t) i].bw->setTooltip (bs.shelf ? "Shelf slope: wide is gentle, narrow is a firmer knee"
                                                           : "Bandwidth in octaves (at full boost)");
        }
        colour.setEnabled (s.colourSource != heron::ColourSource::Off);
        curve.setSettings (s, bypassed);
        lastSettings = s; lastBypass = bypassed; first = false;
    }
}
