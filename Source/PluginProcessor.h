#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "HeronDSP.h"

namespace ids
{
    inline juce::String bandFreq (int b)  { return "b" + juce::String (b + 1) + "_freq"; }
    inline juce::String bandStep (int b)  { return "b" + juce::String (b + 1) + "_step"; }
    inline juce::String bandGain (int b)  { return "b" + juce::String (b + 1) + "_gain"; }
    inline juce::String bandBw (int b)    { return "b" + juce::String (b + 1) + "_bw"; }
    inline juce::String bandShelf (int b) { return "b" + juce::String (b + 1) + "_shelf"; } // bands 1 and 4
    inline const juce::String lowCut      = "lc_freq";
    inline const juce::String lowCutSteep = "lc_steep";
    inline const juce::String colourSrc   = "colour_src";
    inline const juce::String colour      = "colour";
    inline const juce::String bypass      = "bypass";
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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static heron::Settings readSettings (const juce::AudioProcessorValueTreeState&);

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int oversamplingOrder = 1; // 2x
    static constexpr int chunk = 16;            // coefficient update interval (oversampled samples)

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<heron::ChannelEQ> channels;

    // Smoothed control state (shared by all channels: the controls are linked)
    struct BandState { double fc = 1000, gain = 0, bw = 1.5; bool shelf = false; };
    std::array<BandState, 4> bandState;
    double colourState = 0, wetState = 1;
    bool firstBlock = true;
    double osRate = 96000;

    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeronEQAudioProcessor)
};
