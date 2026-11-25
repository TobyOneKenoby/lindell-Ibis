#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::Image loadPng (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); }

    juce::Image frameOf (const juce::Image& strip, int frameSize, int frame)
    {
        return strip.getClippedImage ({ 0, frame * frameSize, frameSize, frameSize });
    }

    bool insideCircle (const juce::Component& c, int x, int y, float fraction)
    {
        const auto b = c.getLocalBounds().toFloat();
        return b.getCentre().getDistanceFrom ({ (float) x, (float) y }) <= b.getWidth() * 0.5f * fraction;
    }
}

//==============================================================================
FilmstripKnob::FilmstripKnob (const juce::Image& s, int fs, int nf, float a0, float a1, bool stepped)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      strip (s), frameSize (fs), numFrames (nf), startDeg (a0), endDeg (a1)
{
    setMouseDragSensitivity (stepped ? 160 : 260);
    setPaintingIsUnclipped (true);
    setRotaryParameters (juce::degreesToRadians (startDeg + 360.0f), juce::degreesToRadians (endDeg + 360.0f), true);
}

void FilmstripKnob::paint (juce::Graphics& g)
{
    const float norm = (float) valueToProportionOfLength (getValue());
    float ang = std::fmod (startDeg + norm * (endDeg - startDeg), 360.0f);
    if (ang < 0) ang += 360.0f;
    const int frame = juce::roundToInt (ang / 360.0f * (float) numFrames) % numFrames;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (frameOf (strip, frameSize, frame), getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

bool FilmstripKnob::hitTest (int x, int y) { return insideCircle (*this, x, y, 0.85f); }

//==============================================================================
StripButton::StripButton (const juce::Image& s, int fs) : juce::Button ({}), strip (s), frameSize (fs)
{
    setClickingTogglesState (true);
}

void StripButton::paintButton (juce::Graphics& g, bool, bool)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (frameOf (strip, frameSize, getToggleState() ? 1 : 0), getLocalBounds().toFloat(),
                 juce::RectanglePlacement::stretchToFit);
}

bool StripButton::hitTest (int x, int y) { return insideCircle (*this, x, y, 0.85f); }

//==============================================================================
FilmstripToggle::FilmstripToggle (const juce::Image& s, int fs) : juce::Button ({}), strip (s), frameSize (fs)
{
    setClickingTogglesState (true);
}

void FilmstripToggle::paintButton (juce::Graphics& g, bool, bool)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (frameOf (strip, frameSize, getToggleState() ? 0 : 1), getLocalBounds().toFloat(),
                 juce::RectanglePlacement::stretchToFit);
}

//==============================================================================
static juce::String paramFor (layout::KnobKind k, int ch, int band)
{
    switch (k)
    {
        case layout::KnobKind::LowCut:       return ids::lowCut (ch);
        case layout::KnobKind::ColourSource: return ids::colourSrc (ch);
        case layout::KnobKind::Colour:       return ids::colour (ch);
        case layout::KnobKind::Gain:         return ids::bandGain (ch, band);
        case layout::KnobKind::Freq:         return ids::bandFreq (ch, band);
        case layout::KnobKind::Bandwidth:    return ids::bandBw (ch, band);
    }
    return {};
}

static juce::String buttonParamFor (const juce::String& name, int ch)
{
    // names: c1_steep, c1_shelf1, c1_shelf4, c1_step1..4
    const auto what = name.fromFirstOccurrenceOf ("_", false, false);
    if (what == "steep") return ids::lowCutSteep (ch);
    if (what.startsWith ("shelf")) return ids::bandShelf (ch, what.getTrailingIntValue() - 1);
    if (what.startsWith ("step")) return ids::bandStep (ch, what.getTrailingIntValue() - 1);
    jassertfalse;
    return {};
}

static bool isStepped (layout::KnobKind k)
{
    return k == layout::KnobKind::LowCut || k == layout::KnobKind::Freq || k == layout::KnobKind::ColourSource;
}

//==============================================================================
HeronEQAudioProcessorEditor::HeronEQAudioProcessorEditor (HeronEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    panel       = loadPng (BinaryData::panel_png, BinaryData::panel_pngSize);
    windowImg   = loadPng (BinaryData::window_png, BinaryData::window_pngSize);
    for (int i = 0; i < fftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
    specIn.fill (-120.0f); specOut.fill (-120.0f);
    knobLarge   = loadPng (BinaryData::knob_large_strip_png, BinaryData::knob_large_strip_pngSize);
    knobSmall   = loadPng (BinaryData::knob_small_strip_png, BinaryData::knob_small_strip_pngSize);
    buttonRed   = loadPng (BinaryData::button_red_strip_png, BinaryData::button_red_strip_pngSize);
    buttonBlack = loadPng (BinaryData::button_black_strip_png, BinaryData::button_black_strip_pngSize);
    buttonWhite = loadPng (BinaryData::button_white_strip_png, BinaryData::button_white_strip_pngSize);
    toggleStrip = loadPng (BinaryData::toggle_strip_png, BinaryData::toggle_strip_pngSize);
    lampStrip   = loadPng (BinaryData::lamp_strip_png, BinaryData::lamp_strip_pngSize);

    for (const auto& k : layout::knobs)
    {
        const auto sw = layout::sweepFor (k.kind);
        auto knob = std::make_unique<FilmstripKnob> (k.large ? knobLarge : knobSmall,
                                                     k.large ? layout::largeKnobFrame : layout::smallKnobFrame,
                                                     layout::knobFrames, sw.startDeg, sw.endDeg, isStepped (k.kind));
        addAndMakeVisible (*knob);
        knobs.push_back ({ &k, std::move (knob), nullptr });
    }
    for (const auto& b : layout::buttons)
    {
        const auto& strip = b.colour == layout::ButtonColour::Red ? buttonRed
                          : b.colour == layout::ButtonColour::Black ? buttonBlack : buttonWhite;
        auto button = std::make_unique<StripButton> (strip, layout::buttonFrame);
        addAndMakeVisible (*button);
        buttons.push_back ({ &b, std::move (button), nullptr });
    }
    for (const auto& t : layout::toggles)
    {
        auto toggle = std::make_unique<FilmstripToggle> (toggleStrip, layout::toggleFrame);
        addAndMakeVisible (*toggle);
        if (juce::String (t.name) == "ch1") ch1Att = std::make_unique<ButtonAtt> (proc.apvts, ids::channelIn (0), *toggle);
        if (juce::String (t.name) == "ch2") ch2Att = std::make_unique<ButtonAtt> (proc.apvts, ids::channelIn (1), *toggle);
        if (juce::String (t.name) == "power") powerSwitch = toggle.get();
        toggles.push_back ({ &t, std::move (toggle) });
    }

    attachChannel (0, 0);
    attachChannel (1, proc.apvts.getRawParameterValue (ids::link)->load() > 0.5f ? 0 : 1);

    // POWER is the inverse of the host bypass parameter.
    powerSwitch->onClick = [this]
    {
        if (auto* bp = proc.apvts.getParameter (ids::bypass))
        {
            bp->beginChangeGesture();
            bp->setValueNotifyingHost (powerSwitch->getToggleState() ? 0.0f : 1.0f);
            bp->endChangeGesture();
        }
    };

    const float aspect = layout::panelW / (layout::panelH + layout::winH);
    setResizable (true, false);
    setResizeLimits (1000, juce::roundToInt (1000 / aspect), 2800, juce::roundToInt (2800 / aspect));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (aspect);
    setSize (1440, juce::roundToInt (1440 / aspect));

    timerCallback();
    startTimerHz (30);
}

HeronEQAudioProcessorEditor::~HeronEQAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> HeronEQAudioProcessorEditor::panelRect (float cx, float cy, float w, float h) const
{
    const float k = (float) getWidth() / layout::panelW;
    return { (cx - w * 0.5f) * k, (cy - h * 0.5f) * k, w * k, h * k };
}

// Binds one half of the panel to a channel's parameters. When linked, the CH2
// half is bound to CH1's parameters, so both halves move together.
void HeronEQAudioProcessorEditor::attachChannel (int panelChannel, int paramChannel)
{
    for (auto& k : knobs)
        if (k.info->channel == panelChannel)
        {
            k.att.reset();
            const auto id = paramFor (k.info->kind, paramChannel, k.info->band - 1);
            k.att = std::make_unique<SliderAtt> (proc.apvts, id, *k.knob);
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
                k.knob->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        }

    for (auto& b : buttons)
        if (juce::String (b.info->name).startsWith ("c" + juce::String (panelChannel + 1)))
        {
            b.att.reset();
            b.att = std::make_unique<ButtonAtt> (proc.apvts, buttonParamFor (b.info->name, paramChannel), *b.button);
        }

    if (panelChannel == 1) boundChannel2 = paramChannel;
}

void HeronEQAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

void HeronEQAudioProcessorEditor::showMenu()
{
    auto* linkParam = proc.apvts.getParameter (ids::link);
    const bool linked = linkParam->getValue() > 0.5f;

    juce::PopupMenu m;
    m.addItem (1, "Link CH1 and CH2 (stereo)", true, linked);
    m.addItem (2, "Copy CH1 settings to CH2", ! linked);
    m.showMenuAsync (juce::PopupMenu::Options(), [this, linkParam, linked] (int r)
    {
        if (r == 1)
        {
            linkParam->beginChangeGesture();
            linkParam->setValueNotifyingHost (linked ? 0.0f : 1.0f);
            linkParam->endChangeGesture();
        }
        else if (r == 2)
        {
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (rp->getParameterID().startsWith ("c1_") && ! rp->getParameterID().endsWith ("_in"))
                        if (auto* dst = proc.apvts.getParameter ("c2_" + rp->getParameterID().substring (3)))
                        {
                            dst->beginChangeGesture();
                            dst->setValueNotifyingHost (rp->getValue());
                            dst->endChangeGesture();
                        }
        }
    });
}

void HeronEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float k = (float) getWidth() / layout::panelW;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (panel, { 0.0f, 0.0f, (float) getWidth(), layout::panelH * k }, juce::RectanglePlacement::stretchToFit);
    g.drawImage (windowImg, { 0.0f, layout::panelH * k, (float) getWidth(), layout::winH * k },
                 juce::RectanglePlacement::stretchToFit);
    paintDisplay (g);

    const float lw = (float) layout::lampFrame / layout::assetScale;
    g.drawImage (frameOf (lampStrip, layout::lampFrame, lampOn ? 1 : 0),
                 panelRect (layout::lampX, layout::lampY, lw, lw), juce::RectanglePlacement::stretchToFit);
}

void HeronEQAudioProcessorEditor::resized()
{
    auto place = [this] (juce::Component& c, float cx, float cy, int frame)
    {
        const float d = (float) frame / layout::assetScale;
        c.setBounds (panelRect (cx, cy, d, d).toNearestInt());
    };
    for (auto& k : knobs)
        place (*k.knob, k.info->x, k.info->y, k.info->large ? layout::largeKnobFrame : layout::smallKnobFrame);
    for (auto& b : buttons)
        place (*b.button, b.info->x, b.info->y, layout::buttonFrame);
    for (auto& t : toggles)
        place (*t.toggle, t.info->x, t.info->y, layout::toggleFrame);
}

void HeronEQAudioProcessorEditor::timerCallback()
{
    const bool linked = proc.apvts.getRawParameterValue (ids::link)->load() > 0.5f;
    if (boundChannel2 != (linked ? 0 : 1))
        attachChannel (1, linked ? 0 : 1);

    const bool power = proc.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    powerSwitch->setToggleState (power, juce::dontSendNotification);

    updateSpectrum();
    repaint (displayArea());
    if (power != lampOn)
    {
        lampOn = power;
        const float lw = (float) layout::lampFrame / layout::assetScale;
        repaint (panelRect (layout::lampX, layout::lampY, lw, lw).toNearestInt().expanded (2));
    }
}

//==============================================================================
// Analyser display: spectrum (as on the Lindell Tracker 2) + the EQ curve
//==============================================================================
juce::Rectangle<float> HeronEQAudioProcessorEditor::winArea (float x0, float y0, float x1, float y1) const
{
    const float k = (float) getWidth() / layout::panelW;
    const float top = layout::panelH * k;
    return { x0 * k, top + y0 * k, (x1 - x0) * k, (y1 - y0) * k };
}

juce::Rectangle<int> HeronEQAudioProcessorEditor::displayArea() const
{
    return winArea (layout::dispX0, layout::dispY0, layout::dispX1, layout::dispY1).toNearestInt().expanded (2);
}

void HeronEQAudioProcessorEditor::updateSpectrum()
{
    const int w = proc.anaWrite.load (std::memory_order_acquire);
    auto analyse = [this, w] (const std::array<float, HeronEQAudioProcessor::analyserSize>& ring, std::array<float, fftSize / 2>& out)
    {
        fftBuf.fill (0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftBuf[(size_t) i] = ring[(size_t) ((w - fftSize + i) & (HeronEQAudioProcessor::analyserSize - 1))] * hann[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        const float norm = 4.0f / (float) fftSize;          // 0 dBFS sine -> 0 dB (Hann coherent gain 0.5)
        for (int i = 0; i < fftSize / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftBuf[(size_t) i] * norm, -120.0f);
            float& v = out[(size_t) i];
            v = db > v ? 0.6f * db + 0.4f * v : juce::jmax (db, v - 1.6f);   // quick rise, smooth fall
        }
    };
    analyse (proc.anaIn, specIn);
    analyse (proc.anaOut, specOut);
}

void HeronEQAudioProcessorEditor::paintDisplay (juce::Graphics& g)
{
    using namespace juce;
    const auto screen = winArea (layout::dispX0, layout::dispY0, layout::dispX1, layout::dispY1).reduced (3.0f);
    const float k = (float) getWidth() / layout::panelW;

    // faint LCD backlight
    ColourGradient back (Colour (0xff0c1d2a).withAlpha (0.85f), screen.getCentreX(), screen.getY(),
                         Colours::transparentBlack, screen.getCentreX(), screen.getBottom(), false);
    g.setGradientFill (back);
    g.fillRoundedRectangle (screen, 5.0f * k);

    const auto plot = winArea (layout::specX0, layout::specY0, layout::specX1, layout::specY1);
    const double fs = jmax (1.0, proc.getSampleRate());
    auto xFor = [&plot] (double f) { return plot.getX() + plot.getWidth() * (float) std::log10 (f / 20.0) / 3.0f; };
    auto freqForX = [&plot] (float x) { return 20.0 * std::pow (1000.0, (double) jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth())); };
    auto yForSpec = [&plot] (float db) { return plot.getY() + plot.getHeight() * jlimit (0.0f, 1.0f, -db / layout::specDb); };
    auto yForEq = [&plot] (float db) { return plot.getY() + plot.getHeight() * (0.5f - db / (2.0f * layout::eqDb)); };

    auto specPath = [&] (const std::array<float, fftSize / 2>& sp, bool closed)
    {
        Path p;
        const float step = jmax (1.5f, 1.5f * k);
        bool first = true;
        if (closed) { p.startNewSubPath (plot.getX(), plot.getBottom()); first = false; }
        for (float x = plot.getX(); x <= plot.getRight(); x += step)
        {
            const float t0 = (x - plot.getX()) / plot.getWidth(), t1 = (x + step - plot.getX()) / plot.getWidth();
            const float b0 = (float) (20.0 * std::pow (1000.0, t0) * fftSize / fs), b1 = (float) (20.0 * std::pow (1000.0, t1) * fftSize / fs);
            float db = -120.0f;
            if (b1 - b0 < 1.0f)
            {
                const int i = jlimit (1, fftSize / 2 - 2, (int) b0);
                const float fr = jlimit (0.0f, 1.0f, b0 - (float) i);
                db = sp[(size_t) i] + fr * (sp[(size_t) i + 1] - sp[(size_t) i]);
            }
            else
                for (int i = jlimit (1, fftSize / 2 - 1, (int) b0); i <= jlimit (1, fftSize / 2 - 1, (int) b1); ++i)
                    db = jmax (db, sp[(size_t) i]);
            const float y = yForSpec (db);
            if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot.toNearestInt().expanded (0, 2));

    // ---- spectrum: input (faint line) and output (filled) -----------------
    g.setColour (Colour (0xffc8d2da).withAlpha (0.28f));
    g.strokePath (specPath (specIn, false), PathStrokeType (1.0f * k));
    const auto out = specPath (specOut, true);
    ColourGradient gr (Colour (0xff3aa7d8).withAlpha (0.50f), 0, plot.getY(), Colour (0xff0f4767).withAlpha (0.12f), 0, plot.getBottom(), false);
    g.setGradientFill (gr);
    g.fillPath (out);
    g.setColour (Colour (0xff7fd2f2).withAlpha (0.75f));
    g.strokePath (out, PathStrokeType (1.0f * k));

    // ---- EQ curve(s) ------------------------------------------------------
    const bool linked = proc.apvts.getRawParameterValue (ids::link)->load() > 0.5f;
    const bool power = proc.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    const int numCurves = linked ? 1 : 2;
    const Colour glow[2] = { Colour (0xffffd9a0), Colour (0xffa0ffe0) };
    const Colour line[2] = { Colour (0xfff3e3bf), Colour (0xffbff3e3) };

    for (int c = numCurves - 1; c >= 0; --c)
    {
        const auto s = HeronEQAudioProcessor::readSettings (proc.apvts, c);
        const bool in = power && proc.apvts.getRawParameterValue (ids::channelIn (c))->load() > 0.5f;
        const float alpha = in ? 1.0f : 0.35f;

        Path curve;
        for (float x = plot.getX(); x <= plot.getRight() + 1.0f; x += 2.0f)
        {
            const float db = in ? (float) (20.0 * std::log10 (jmax (1.0e-6, heron::magnitudeAt (s, freqForX (x))))) : 0.0f;
            const float y = yForEq (jlimit (-layout::eqDb - 2.0f, layout::eqDb + 2.0f, db));
            if (x == plot.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (glow[c].withAlpha (0.16f * alpha));
        g.strokePath (curve, PathStrokeType (7.0f * k, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (line[c].withAlpha (alpha));
        g.strokePath (curve, PathStrokeType (1.8f * k, PathStrokeType::curved, PathStrokeType::rounded));

        // band markers 1-4 at each band's frequency, on the curve
        g.setFont (FontOptions (9.5f * k * 1.2f, Font::bold));
        for (int b = 0; b < 4; ++b)
        {
            const auto& bs = s.bands[(size_t) b];
            const double f = heron::freqs::hzFor (b, bs.position, bs.plusStep);
            const bool active = heron::applyDeadZone (bs.gainDb) != 0.0 && in;
            const float db = in ? (float) (20.0 * std::log10 (jmax (1.0e-6, heron::magnitudeAt (s, f)))) : 0.0f;
            const Point<float> pt (xFor (f), yForEq (jlimit (-layout::eqDb, layout::eqDb, db)));
            const float r = 6.5f * k;
            g.setColour (Colour (0xff0c1d2a).withAlpha (0.85f));
            g.fillEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r);
            g.setColour (line[c].withAlpha (active ? 1.0f : 0.40f));
            g.drawEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r, 1.2f * k);
            g.drawText (String (b + 1), Rectangle<float> (pt.x - r, pt.y - r, 2 * r, 2 * r), Justification::centred);
        }
    }

    if (! linked)
    {
        g.setFont (FontOptions (8.0f * k * 1.2f, Font::bold));
        for (int c = 0; c < 2; ++c)
        {
            g.setColour (line[c]);
            g.drawText (c == 0 ? "CH1 (L)" : "CH2 (R)", Rectangle<float> (plot.getRight() - 150.0f * k + (float) c * 70.0f * k,
                        plot.getY() + 14.0f * k, 66.0f * k, 12.0f * k), Justification::centredRight);
        }
    }
}
