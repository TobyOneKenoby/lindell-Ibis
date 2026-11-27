#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "HeronDSP.h"

// Parameter IDs. Every channel control exists twice: "c1_..." (left) and "c2_..." (right).
namespace ids
{
    inline juce::String ch (int c, const juce::String& id) { return "c" + juce::String (c + 1) + "_" + id; }
    inline juce::String bandFreq (int c, int b)  { return ch (c, "b" + juce::String (b + 1) + "_freq"); }
    inline juce::String bandStep (int c, int b)  { return ch (c, "b" + juce::String (b + 1) + "_step"); }
    inline juce::String bandGain (int c, int b)  { return ch (c, "b" + juce::String (b + 1) + "_gain"); }
    inline juce::String bandBw (int c, int b)    { return ch (c, "b" + juce::String (b + 1) + "_bw"); }
    inline juce::String bandShelf (int c, int b) { return ch (c, "b" + juce::String (b + 1) + "_shelf"); } // bands 1 and 4
    inline juce::String lowCut (int c)           { return ch (c, "lc_freq"); }
    inline juce::String lowCutSteep (int c)      { return ch (c, "lc_steep"); }
    inline juce::String colourSrc (int c)        { return ch (c, "colour_src"); }
    inline juce::String colour (int c)           { return ch (c, "colour"); }
    inline juce::String channelIn (int c)        { return ch (c, "in"); }
    inline const juce::String link   = "link";
    inline const juce::String bypass = "bypass";   // the POWER switch (inverted)
}

class HeronEQAudioProcessor : public juce::AudioProcessor
{
public:
    HeronEQAudioProcessor();
    ~HeronEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** OVERSAMPLING 2 / 4 / 8 / 16 x (at 44.1 / 48 kHz; saved with the session) */
    int getOversampling() const;
    void setOversampling (int factor);
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static heron::Settings readSettings (const juce::AudioProcessorValueTreeState&, int channel);

    // Knob tapers: the printed scale marks are evenly spaced around the knob.
    static const std::vector<float>& gainAnchors();
    static const std::vector<float>& bandwidthAnchors();

    juce::AudioProcessorValueTreeState apvts;

    // Analyser feed: mono (L+R)/2 of the input and the output, written by the audio
    // thread into rings and read by the editor's display (single producer / consumer).
    static constexpr int analyserSize = 16384;     // power of two
    std::array<float, analyserSize> anaIn {}, anaOut {};
    std::atomic<int> anaWrite { 0 };

private:
    int oversamplingOrder = 1;                  // 2x at 44.1 / 48 kHz by default
    int preparedBlockSize = 512;
    int osLog2For (double sampleRate) const;
    static constexpr int chunk = 16;            // coefficient update interval (oversampled samples)

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<heron::ChannelEQ> channels;

    struct BandState { double fc = 1000, gain = 0, bw = 1.5; bool shelf = false; };
    struct ChannelState { std::array<BandState, 4> bands; double colour = 0, wet = 1; };
    std::array<ChannelState, 2> state;
    bool firstBlock = true;
    double osRate = 96000;

    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessor)
};
