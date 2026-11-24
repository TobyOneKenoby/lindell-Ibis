#include "PluginEditor.h"
#include "BinaryData.h"

using namespace heron::ui;
namespace hp = heron::panel;

//==============================================================================
juce::Image heron::ui::loadPanelImage (const char* resourceName)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (resourceName, size))
        return juce::ImageCache::getFromMemory (data, size);
    jassertfalse;
    return {};
}

//==============================================================================
SpriteKnob::SpriteKnob (const hp::Knob& info, float startDeg, float endDeg)
    : sprite (loadPanelImage (info.resource)),
      pivot (info.pivotX - (float) info.x, info.pivotY - (float) info.y),
      photoAngle (info.photoAngleDeg), startAngle (startDeg), endAngle (endDeg),
      radius ((float) info.size * 0.5f)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);
    setBufferedToImage (false);
    setBounds (info.x, info.y, info.size, info.size);
    setPaintingIsUnclipped (true);
}

void SpriteKnob::paint (juce::Graphics& g)
{
    const float t = (float) valueToProportionOfLength (getValue());
    const float angle = startAngle + t * (endAngle - startAngle);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (sprite, juce::AffineTransform::rotation (juce::degreesToRadians (angle - photoAngle), pivot.x, pivot.y));
}

bool SpriteKnob::hitTest (int x, int y)
{
    return pivot.getDistanceFrom ({ (float) x, (float) y }) <= radius;
}

//==============================================================================
SpriteButton::SpriteButton (const hp::Sprite& info)
    : juce::Button (info.name), pressed (loadPanelImage (info.resource))
{
    setClickingTogglesState (true);
    setBounds (info.x, info.y, info.size, info.size);
}

void SpriteButton::paintButton (juce::Graphics& g, bool, bool)
{
    if (getToggleState())
        g.drawImageAt (pressed, 0, 0);
}

bool SpriteButton::hitTest (int x, int y)
{
    return juce::Point<int> (x, y).getDistanceFrom (getLocalBounds().getCentre()) <= getWidth() / 2;
}

//==============================================================================
ToggleSwitch::ToggleSwitch (const hp::Sprite& info)
    : juce::Button (info.name), leverRight (loadPanelImage (info.resource))
{
    setClickingTogglesState (true);
    setBounds (info.x, info.y, info.size, info.size);
}

void ToggleSwitch::paintButton (juce::Graphics& g, bool, bool)
{
    if (! getToggleState())
        g.drawImageAt (leverRight, 0, 0);
}

//==============================================================================
Lamp::Lamp() : off (loadPanelImage (hp::lampOff.resource))
{
    setInterceptsMouseClicks (false, false);
    setBounds (hp::lampOff.x, hp::lampOff.y, hp::lampOff.size, hp::lampOff.size);
}

void Lamp::paint (juce::Graphics& g)
{
    if (! lit)
        g.drawImageAt (off, 0, 0);
}

//==============================================================================
HeronEQAudioProcessorEditor::Panel::Panel() : image (loadPanelImage ("panel_png"))
{
    setSize (hp::width, hp::height);
}

void HeronEQAudioProcessorEditor::Panel::paint (juce::Graphics& g)
{
    g.drawImageAt (image, 0, 0);
}

void HeronEQAudioProcessorEditor::Panel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu)
        onMenu();
}

//==============================================================================
// Rotation range of each knob type, matching the printed scale (degrees from 12 o'clock).
static std::pair<float, float> rangeFor (hp::KnobKind k)
{
    switch (k)
    {
        case hp::KnobKind::LowCut:       return { -165.0f, 165.0f };  // OUT, 20 ... 150 (30 deg steps)
        case hp::KnobKind::Freq:         return { -165.0f, 165.0f };  // 12 note positions (30 deg steps)
        case hp::KnobKind::ColourSource: return { -62.0f, 38.0f };    // OFF, 1, 2, 3, 4, FULL (20 deg steps)
        case hp::KnobKind::Colour:       return { -150.0f, 150.0f };  // 0 ... 10
        case hp::KnobKind::Gain:         return { -148.0f, 148.0f };  // 12 ... 0 ... 12
        case hp::KnobKind::Bandwidth:    return { -150.0f, 150.0f };  // .1 ... 4
    }
    return { -150.0f, 150.0f };
}

static juce::String paramFor (hp::KnobKind k, int ch, int band)
{
    switch (k)
    {
        case hp::KnobKind::LowCut:       return ids::lowCut (ch);
        case hp::KnobKind::ColourSource: return ids::colourSrc (ch);
        case hp::KnobKind::Colour:       return ids::colour (ch);
        case hp::KnobKind::Gain:         return ids::bandGain (ch, band);
        case hp::KnobKind::Freq:         return ids::bandFreq (ch, band);
        case hp::KnobKind::Bandwidth:    return ids::bandBw (ch, band);
    }
    return {};
}

static juce::String buttonParamFor (const juce::String& name, int ch)
{
    // names: c1_shelf1, c1_shelf4, c1_steep, c1_step1..4
    const auto what = name.fromFirstOccurrenceOf ("_", false, false);
    if (what == "steep") return ids::lowCutSteep (ch);
    if (what.startsWith ("shelf")) return ids::bandShelf (ch, what.getTrailingIntValue() - 1);
    if (what.startsWith ("step")) return ids::bandStep (ch, what.getTrailingIntValue() - 1);
    jassertfalse;
    return {};
}

HeronEQAudioProcessorEditor::HeronEQAudioProcessorEditor (HeronEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (panel);
    panel.onMenu = [this] { showMenu(); };

    for (const auto& k : hp::knobs)
    {
        const auto range = rangeFor (k.kind);
        KnobSlot slot { &k, std::make_unique<SpriteKnob> (k, range.first, range.second), nullptr };
        panel.addAndMakeVisible (*slot.knob);
        knobs.push_back (std::move (slot));
    }
    for (const auto& b : hp::buttons)
    {
        ButtonSlot slot { b.name, std::make_unique<SpriteButton> (b), nullptr };
        panel.addAndMakeVisible (*slot.button);
        buttons.push_back (std::move (slot));
    }
    for (const auto& t : hp::toggles)
    {
        auto sw = std::make_unique<ToggleSwitch> (t);
        panel.addAndMakeVisible (*sw);
        const juce::String n (t.name);
        if (n == "ch1") ch1Switch = std::move (sw);
        else if (n == "ch2") ch2Switch = std::move (sw);
        else powerSwitch = std::move (sw);
    }
    panel.addAndMakeVisible (lamp);

    attachChannel (0, 0);
    attachChannel (1, proc.apvts.getRawParameterValue (ids::link)->load() > 0.5f ? 0 : 1);

    // Channel IN/OUT switches stay per channel even when the controls are linked.
    ch1Att = std::make_unique<ButtonAtt> (proc.apvts, ids::channelIn (0), *ch1Switch);
    ch2Att = std::make_unique<ButtonAtt> (proc.apvts, ids::channelIn (1), *ch2Switch);

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

    setResizable (true, true);
    setResizeLimits (hp::width / 2, hp::height / 2, hp::width * 5 / 4, hp::height * 5 / 4);
    getConstrainer()->setFixedAspectRatio ((double) hp::width / hp::height);
    setSize (hp::width * 3 / 4, hp::height * 3 / 4);

    timerCallback();
    startTimerHz (20);
}

HeronEQAudioProcessorEditor::~HeronEQAudioProcessorEditor() { stopTimer(); }

// Binds one half of the panel (panelChannel) to the parameters of paramChannel.
// When linked, the CH2 half is bound to CH1's parameters, so both halves move together.
void HeronEQAudioProcessorEditor::attachChannel (int panelChannel, int paramChannel)
{
    for (auto& k : knobs)
        if (k.info->channel - 1 == panelChannel)
        {
            k.att.reset();
            const auto id = paramFor (k.info->kind, paramChannel, k.info->band - 1);
            k.att = std::make_unique<SliderAtt> (proc.apvts, id, *k.knob);
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
                k.knob->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        }

    for (auto& b : buttons)
        if (b.name.startsWith ("c" + juce::String (panelChannel + 1)))
        {
            b.att.reset();
            b.att = std::make_unique<ButtonAtt> (proc.apvts, buttonParamFor (b.name, paramChannel), *b.button);
        }

    if (panelChannel == 1) boundChannel2 = paramChannel;
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

void HeronEQAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void HeronEQAudioProcessorEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) hp::width));
}

void HeronEQAudioProcessorEditor::timerCallback()
{
    const bool linked = proc.apvts.getRawParameterValue (ids::link)->load() > 0.5f;
    if (boundChannel2 != (linked ? 0 : 1))
        attachChannel (1, linked ? 0 : 1);

    const bool power = proc.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    powerSwitch->setToggleState (power, juce::dontSendNotification);
    lamp.setLit (power);
}
