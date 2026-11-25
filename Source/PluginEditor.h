#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
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
    juce::Rectangle<float> winArea (float x0, float y0, float x1, float y1) const;   // display-plate coords
    juce::Rectangle<int> displayArea() const;
    void updateSpectrum();
    void paintDisplay (juce::Graphics&);

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    HeronEQAudioProcessor& proc;
    juce::Image windowImg, panel, knobLarge, knobSmall, buttonRed, buttonBlack, buttonWhite, toggleStrip, lampStrip;

    struct KnobSlot { const layout::Knob* info; std::unique_ptr<FilmstripKnob> knob; std::unique_ptr<SliderAtt> att; };
    struct ButtonSlot { const layout::Button* info; std::unique_ptr<StripButton> button; std::unique_ptr<ButtonAtt> att; };
    struct ToggleSlot { const layout::Toggle* info; std::unique_ptr<FilmstripToggle> toggle; };
    std::vector<KnobSlot> knobs;
    std::vector<ButtonSlot> buttons;
    std::vector<ToggleSlot> toggles;
    std::unique_ptr<ButtonAtt> ch1Att, ch2Att;
    FilmstripToggle* powerSwitch = nullptr;

    bool lampOn = true;

    // analyser (same as the Lindell Tracker 2 WINDOW display)
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> hann {};
    std::array<float, fftSize * 2> fftBuf {};
    std::array<float, fftSize / 2> specIn {}, specOut {};
    int boundChannel2 = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessorEditor)
};
