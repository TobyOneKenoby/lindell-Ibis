#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace heron::ui
{
    namespace colours
    {
        const juce::Colour panel      { 0xff1b1d20 };
        const juce::Colour panelLight { 0xff26292d };
        const juce::Colour groove     { 0xff121315 };
        const juce::Colour text       { 0xffe9e2d2 };
        const juce::Colour textDim    { 0xff8f8a80 };
        const juce::Colour copper     { 0xffd2864a };
        const juce::Colour slate      { 0xff6f8796 };
        const juce::Colour bone       { 0xffbdb6a6 };
        const juce::Colour ledRed     { 0xffff5a3c };
        const juce::Colour ledTeal    { 0xff3fd0c0 };
        const juce::Colour ledAmber   { 0xffffc04a };
        const juce::Colour ledGreen   { 0xff6cff8a };
        const juce::Colour bandColours[4] = { juce::Colour (0xffe0814a), juce::Colour (0xffd9c25a),
                                              juce::Colour (0xff6cc7a8), juce::Colour (0xff7aa8e0) };
    }

    class HeronLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HeronLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Font getLabelFont (juce::Label&) override;
    };

    // Knob with a role (sets cap colour and number of scale ticks).
    class Knob : public juce::Slider
    {
    public:
        enum class Role { Gain, Freq, Bandwidth, Utility };
        Knob (Role r, int stepsIn = 0) : role (r), steps (stepsIn)
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        }
        Role role;
        int steps; // 0 = continuous
    };

    class LedButton : public juce::ToggleButton
    {
    public:
        LedButton (const juce::String& label, juce::Colour led) : juce::ToggleButton (label), ledColour (led) {}
        juce::Colour ledColour;
    };

    class ResponseCurve : public juce::Component
    {
    public:
        void setSettings (const heron::Settings& s, bool bypassed);
        void paint (juce::Graphics&) override;
    private:
        heron::Settings settings;
        bool isBypassed = false;
    };
}

class HeronEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HeronEQAudioProcessorEditor (HeronEQAudioProcessor&);
    ~HeronEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutPanel();

    using Knob = heron::ui::Knob;
    using LedButton = heron::ui::LedButton;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    static constexpr int baseW = 1000, baseH = 560;

    HeronEQAudioProcessor& proc;
    heron::ui::HeronLookAndFeel lnf;

    struct Panel : juce::Component
    {
        void paint (juce::Graphics&) override;
    } panel;

    heron::ui::ResponseCurve curve;

    struct BandUI
    {
        std::unique_ptr<Knob> gain, freq, bw;
        std::unique_ptr<LedButton> step, shelf;
        juce::Label freqReadout;
        std::unique_ptr<SliderAtt> gainAtt, freqAtt, bwAtt;
        std::unique_ptr<ButtonAtt> stepAtt, shelfAtt;
    };
    std::array<BandUI, 4> bands;

    Knob lowCut { Knob::Role::Freq, heron::freqs::numLowCut };
    Knob colourSource { Knob::Role::Utility, 6 };
    Knob colour { Knob::Role::Utility };
    LedButton steep { "STEEP", heron::ui::colours::ledAmber };
    LedButton inButton { "IN", heron::ui::colours::ledGreen };
    std::unique_ptr<SliderAtt> lowCutAtt, colourSourceAtt, colourAtt;
    std::unique_ptr<ButtonAtt> steepAtt;

    heron::Settings lastSettings;
    bool lastBypass = false, first = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessorEditor)
};
