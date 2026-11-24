#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace heron;

//==============================================================================
static juce::String formatHz (double hz)
{
    if (hz >= 1000.0)
        return juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + "k";
    return hz < 100.0 ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz));
}

juce::AudioProcessorValueTreeState::ParameterLayout HeronEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < 4; ++b)
    {
        const String name = "Band " + String (b + 1) + " ";

        StringArray freqLabels;
        for (int p = 0; p < freqs::numPositions; ++p)
        {
            const int note = freqs::noteFor (b, p, false);
            freqLabels.add (String (freqs::noteName (note)) + " " + formatHz (freqs::noteToHz (note)));
        }
        const int defaultPos[4] = { 3, 5, 6, 7 };
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::bandFreq (b), 1 }, name + "Freq", freqLabels, defaultPos[b]));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bandStep (b), 1 }, name + "+1 Step", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::bandGain (b), 1 }, name + "Gain",
            NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                {
                    const double g = applyDeadZone (v);
                    return g == 0.0 ? String ("0.0 dB") : (g > 0 ? "+" : "") + String (g, 1) + " dB";
                })));

        NormalisableRange<float> bwRange (0.2f, 4.0f,
            [] (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); },
            [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); });
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids::bandBw (b), 1 }, name + "Bandwidth", bwRange, 1.5f,
            AudioParameterFloatAttributes()
                .withLabel ("oct")
                .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; })));

        if (b == 0 || b == 3)
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bandShelf (b), 1 }, name + "Shelf", false));
    }

    StringArray lcLabels { "Out" };
    for (int i = 1; i < freqs::numLowCut; ++i) lcLabels.add (String ((int) freqs::lowCutHz[i]) + " Hz");
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::lowCut, 1 }, "Low Cut", lcLabels, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lowCutSteep, 1 }, "Low Cut Steep", false));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::colourSrc, 1 }, "Colour Source",
        StringArray { "Off", "Band 1", "Band 2", "Band 3", "Band 4", "Full" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::colour, 1 }, "Colour",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return layout;
}

heron::Settings HeronEQAudioProcessor::readSettings (const juce::AudioProcessorValueTreeState& s)
{
    auto get = [&] (const juce::String& id) { return s.getRawParameterValue (id)->load(); };
    heron::Settings out;
    for (int b = 0; b < 4; ++b)
    {
        auto& band = out.bands[(size_t) b];
        band.position = (int) get (ids::bandFreq (b));
        band.plusStep = get (ids::bandStep (b)) > 0.5f;
        band.gainDb = get (ids::bandGain (b));
        band.bandwidthOct = get (ids::bandBw (b));
        band.shelf = (b == 0 || b == 3) && get (ids::bandShelf (b)) > 0.5f;
    }
    out.lowCutIndex = (int) get (ids::lowCut);
    out.lowCutSteep = get (ids::lowCutSteep) > 0.5f;
    out.colourSource = (ColourSource) (int) get (ids::colourSrc);
    out.colour = get (ids::colour);
    return out;
}

//==============================================================================
HeronEQAudioProcessor::HeronEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HeronEQ", createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ids::bypass));
}

juce::AudioProcessorParameter* HeronEQAudioProcessor::getBypassParameter() const { return bypassParam; }

bool HeronEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void HeronEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, oversamplingOrder, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    osRate = sampleRate * (1 << oversamplingOrder);
    channels.assign ((size_t) numCh, ChannelEQ {});
    for (auto& c : channels) c.prepare (osRate);
    firstBlock = true;
}

void HeronEQAudioProcessor::releaseResources()
{
    if (oversampler) oversampler->reset();
}

void HeronEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh == 0 || buffer.getNumSamples() == 0) return;

    // --- Targets from the current parameter values ---
    const Settings s = readSettings (apvts);
    BandState target[4];
    for (int b = 0; b < 4; ++b)
    {
        const auto& bs = s.bands[(size_t) b];
        target[b].fc = freqs::hzFor (b, juce::jlimit (0, 11, bs.position), bs.plusStep);
        target[b].gain = applyDeadZone (bs.gainDb);
        target[b].bw = bs.bandwidthOct;
        target[b].shelf = bs.shelf;
    }
    const double colourTarget = s.colour / 10.0;
    const double wetTarget = bypassParam->get() ? 0.0 : 1.0;
    const double lowCutHz = freqs::lowCutHz[juce::jlimit (0, freqs::numLowCut - 1, s.lowCutIndex)];

    if (firstBlock)
    {
        for (int b = 0; b < 4; ++b) bandState[(size_t) b] = target[b];
        colourState = colourTarget; wetState = wetTarget;
        for (auto& c : channels) c.reset();
        firstBlock = false;
    }

    // --- Oversampled processing ---
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) buffer.getNumSamples());
    auto os = oversampler->processSamplesUp (block);
    const int n = (int) os.getNumSamples();

    // About 20 ms one-pole smoothing, applied once per chunk.
    const double a = 1.0 - std::exp (-(double) chunk / (0.02 * osRate));

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);

        std::array<ChannelEQ::BandCoeffs, 4> bc;
        for (int b = 0; b < 4; ++b)
        {
            auto& st = bandState[(size_t) b];
            st.fc *= std::pow (target[b].fc / st.fc, a);   // glide in log-frequency
            st.gain += (target[b].gain - st.gain) * a;
            st.bw *= std::pow (target[b].bw / st.bw, a);
            st.shelf = target[b].shelf;

            auto& c = bc[(size_t) b];
            c.fc = st.fc;
            if (st.shelf) { c.mode = b == 0 ? SVF::Mode::LowPass : SVF::Mode::HighPass; c.q = bandwidthToShelfQ (st.bw); }
            else          { c.mode = SVF::Mode::BandPass; c.q = bandwidthToQ (st.bw); }
            const double A = std::pow (10.0, st.gain / 20.0);
            c.k = st.gain > 0 ? A - 1.0 : 0.0;
            c.m = st.gain < 0 ? 1.0 / A - 1.0 : 0.0;
        }
        colourState += (colourTarget - colourState) * a;

        const double wetStart = wetState;
        wetState += (wetTarget - wetState) * a;
        if (std::abs (wetState - wetTarget) < 1e-4) wetState = wetTarget;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& eq = channels[(size_t) ch];
            eq.update (bc, lowCutHz, s.lowCutSteep, s.colourSource, colourState);
            float* d = os.getChannelPointer ((size_t) ch) + start;
            for (int i = 0; i < len; ++i)
            {
                const double w = wetStart + (wetState - wetStart) * (i + 1) / len;
                const double x = d[i];
                const double y = eq.process (x);
                d[i] = (float) (w * y + (1.0 - w) * x);
            }
        }
    }

    oversampler->processSamplesDown (block);
}

//==============================================================================
juce::AudioProcessorEditor* HeronEQAudioProcessor::createEditor() { return new HeronEQAudioProcessorEditor (*this); }

void HeronEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HeronEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HeronEQAudioProcessor(); }
