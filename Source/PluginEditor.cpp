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

    const float aspect = layout::panelW / layout::panelH;
    setResizable (true, false);
    setResizeLimits (1000, juce::roundToInt (1000 / aspect), 2800, juce::roundToInt (2800 / aspect));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (aspect);
    setSize (1440, juce::roundToInt (1440 / aspect));

    timerCallback();
    startTimerHz (20);
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
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (panel, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

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
    if (power != lampOn)
    {
        lampOn = power;
        const float lw = (float) layout::lampFrame / layout::assetScale;
        repaint (panelRect (layout::lampX, layout::lampY, lw, lw).toNearestInt().expanded (2));
    }
}
