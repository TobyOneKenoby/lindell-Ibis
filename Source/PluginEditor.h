#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Layout.h"

/** A rotary control drawn from a pre-rendered filmstrip covering 360 degrees. */
class FilmstripKnob : public juce::Slider
{
public:
    FilmstripKnob (const juce::Image& strip, int frameSize, int numFrames, float startDeg, float endDeg, bool stepped);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

private:
    juce::Image strip;
    int frameSize, numFrames;
    float startDeg, endDeg;
};

/** Latching push button from a 2-frame strip: frame 0 = out, frame 1 = pushed in. */
class StripButton : public juce::Button
{
public:
    StripButton (const juce::Image& strip, int frameSize);
    void paintButton (juce::Graphics&, bool, bool) override;
    bool hitTest (int x, int y) override;

private:
    juce::Image strip;
    int frameSize;
};

/** Bat toggle seen head-on: frame 0 = lever left (IN / ON), frame 1 = lever right. */
class FilmstripToggle : public juce::Button
{
public:
    FilmstripToggle (const juce::Image& strip, int frameSize);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    juce::Image strip;
    int frameSize;
};

class HeronEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HeronEQAudioProcessorEditor (HeronEQAudioProcessor&);
    ~HeronEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void attachChannel (int panelChannel, int paramChannel);
    void showMenu();
    juce::Rectangle<float> panelRect (float cx, float cy, float w, float h) const;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    HeronEQAudioProcessor& proc;
    juce::Image panel, knobLarge, knobSmall, buttonRed, buttonBlack, buttonWhite, toggleStrip, lampStrip;

    struct KnobSlot { const layout::Knob* info; std::unique_ptr<FilmstripKnob> knob; std::unique_ptr<SliderAtt> att; };
    struct ButtonSlot { const layout::Button* info; std::unique_ptr<StripButton> button; std::unique_ptr<ButtonAtt> att; };
    struct ToggleSlot { const layout::Toggle* info; std::unique_ptr<FilmstripToggle> toggle; };
    std::vector<KnobSlot> knobs;
    std::vector<ButtonSlot> buttons;
    std::vector<ToggleSlot> toggles;
    std::unique_ptr<ButtonAtt> ch1Att, ch2Att;
    FilmstripToggle* powerSwitch = nullptr;

    bool lampOn = true;
    int boundChannel2 = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessorEditor)
};
