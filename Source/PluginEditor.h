#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "Layout.h"

/** A filmstrip (or single image) kept pre-scaled, with an area-averaging
    resampler, to the exact physical pixel size it is shown at. Drawing from
    it is then 1:1, so the rendered art stays sharp at any size and on retina. */
class ScaledStrip
{
public:
    ScaledStrip() = default;
    ScaledStrip (const juce::Image& source, int frameW, int frameH, int numFrames);
    /** The frame scaled to w x h physical pixels (re-scales only when the size changes). */
    juce::Image frame (int index, int w, int h);
    int numFrames() const { return frames; }

private:
    juce::Image src, scaled;
    int srcW = 0, srcH = 0, frames = 1, curW = -1, curH = -1;
};

/** A rotary control drawn from a pre-rendered filmstrip covering 360 degrees. */
class FilmstripKnob : public juce::Slider
{
public:
    FilmstripKnob (ScaledStrip& strip, float startDeg, float endDeg, bool stepped);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

private:
    ScaledStrip& strip;
    float startDeg, endDeg;
};

/** Latching push button from a 2-frame strip: frame 0 = out, frame 1 = pushed in. */
class StripButton : public juce::Button
{
public:
    explicit StripButton (ScaledStrip& strip);
    void paintButton (juce::Graphics&, bool, bool) override;
    bool hitTest (int x, int y) override;

private:
    ScaledStrip& strip;
};

/** Bat toggle seen head-on: frame 0 = lever left (IN / ON), frame 1 = lever right. */
class FilmstripToggle : public juce::Button
{
public:
    explicit FilmstripToggle (ScaledStrip& strip);
    void paintButton (juce::Graphics&, bool, bool) override;

private:
    ScaledStrip& strip;
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
    ScaledStrip panel, windowImg, knobLarge, knobSmall, buttonRed, buttonBlack, buttonWhite, buttonRing, toggleStrip, lampStrip;

    struct KnobSlot { const layout::Knob* info; std::unique_ptr<FilmstripKnob> knob; std::unique_ptr<SliderAtt> att; };
    struct ButtonSlot { const layout::Button* info; std::unique_ptr<StripButton> button; std::unique_ptr<ButtonAtt> att; };
    struct ToggleSlot { const layout::Toggle* info; std::unique_ptr<StripButton> toggle; };   // push buttons
    std::vector<KnobSlot> knobs;
    std::vector<ButtonSlot> buttons;
    std::vector<ToggleSlot> toggles;
    std::unique_ptr<ButtonAtt> ch1Att, ch2Att, linkAtt;
    StripButton* powerSwitch = nullptr;
    StripButton* displaySwitch = nullptr;
    // green LED ring round every push button, lit while the button is in
    void drawRing (juce::Graphics&, float cx, float cy, bool lit);
    juce::uint64 ringState = 0;           // last drawn on/off pattern (repaint when it changes)
    juce::uint64 currentRingState() const;

    // DISPLAY switch: shows / hides the analyser plate (saved with the plug-in state)
    bool displayOn = true;
    void setDisplayOn (bool on);
    void applyWindowSize (int width);    // keeps the panel aspect; Size menu 70 .. 200 %
    float contentH() const { return layout::panelH + (displayOn ? layout::winH : 0.0f); }

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
