#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PanelLayout.h"

namespace heron::ui
{
    juce::Image loadPanelImage (const char* resourceName);

    // A knob drawn by rotating its photographed cap around its centre.
    class SpriteKnob : public juce::Slider
    {
    public:
        SpriteKnob (const heron::panel::Knob& info, float startDeg, float endDeg);
        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
    private:
        juce::Image sprite;
        juce::Point<float> pivot;
        float photoAngle, startAngle, endAngle, radius;
    };

    // A latching push button. Released = the panel photo; pressed = a sunk-in bitmap.
    class SpriteButton : public juce::Button
    {
    public:
        explicit SpriteButton (const heron::panel::Sprite& info);
        void paintButton (juce::Graphics&, bool, bool) override;
        bool hitTest (int x, int y) override;
    private:
        juce::Image pressed;
    };

    // A bat-handle toggle. ON = lever left (as photographed), OFF = lever right.
    class ToggleSwitch : public juce::Button
    {
    public:
        explicit ToggleSwitch (const heron::panel::Sprite& info);
        void paintButton (juce::Graphics&, bool, bool) override;
    private:
        juce::Image leverRight;
    };

    class Lamp : public juce::Component
    {
    public:
        Lamp();
        void setLit (bool shouldBeLit) { if (lit != shouldBeLit) { lit = shouldBeLit; repaint(); } }
        void paint (juce::Graphics&) override;
    private:
        juce::Image off;
        bool lit = true;
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
    void attachChannel (int panelChannel, int paramChannel);
    void showMenu();

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    HeronEQAudioProcessor& proc;

    struct Panel : juce::Component
    {
        Panel();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::Image image;
        std::function<void()> onMenu;
    } panel;

    struct KnobSlot { const heron::panel::Knob* info; std::unique_ptr<heron::ui::SpriteKnob> knob; std::unique_ptr<SliderAtt> att; };
    struct ButtonSlot { juce::String name; std::unique_ptr<heron::ui::SpriteButton> button; std::unique_ptr<ButtonAtt> att; };
    std::vector<KnobSlot> knobs;
    std::vector<ButtonSlot> buttons;

    std::unique_ptr<heron::ui::ToggleSwitch> ch1Switch, ch2Switch, powerSwitch;
    std::unique_ptr<ButtonAtt> ch1Att, ch2Att;
    heron::ui::Lamp lamp;

    int boundChannel2 = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessorEditor)
};
