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

// A range whose normalised position moves evenly between the given anchor values,
// so the knob lines up with the panel's printed scale.
static juce::NormalisableRange<float> piecewiseRange (std::vector<float> anchors)
{
    const float lo = anchors.front(), hi = anchors.back();
    auto from01 = [anchors] (float, float, float t)
    {
        const float pos = juce::jlimit (0.0f, 1.0f, t) * (float) (anchors.size() - 1);
        const int i = juce::jmin ((int) pos, (int) anchors.size() - 2);
        return anchors[(size_t) i] + (pos - (float) i) * (anchors[(size_t) i + 1] - anchors[(size_t) i]);
    };
    auto to01 = [anchors] (float, float, float v)
    {
        v = juce::jlimit (anchors.front(), anchors.back(), v);
        for (size_t i = 0; i + 1 < anchors.size(); ++i)
            if (v <= anchors[i + 1])
                return ((float) i + (v - anchors[i]) / (anchors[i + 1] - anchors[i])) / (float) (anchors.size() - 1);
        return 1.0f;
    };
    return juce::NormalisableRange<float> (lo, hi, from01, to01);
}

const std::vector<float>& HeronEQAudioProcessor::gainAnchors()
{
    static const std::vector<float> a { -12, -10, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 10, 12 };
    return a;
}

const std::vector<float>& HeronEQAudioProcessor::bandwidthAnchors()
{
    // Marks on the panel: .1  .25  1  1.5  2  .  3  .  3.5  .  4
    static const std::vector<float> a { 0.1f, 0.25f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.25f, 3.5f, 3.75f, 4.0f };
    return a;
}

juce::AudioProcessorValueTreeState::ParameterLayout HeronEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int c = 0; c < 2; ++c)
    {
        const String chName = "CH" + String (c + 1) + " ";
        for (int b = 0; b < 4; ++b)
        {
            const String name = chName + "Band " + String (b + 1) + " ";

            StringArray freqLabels;
            for (int p = 0; p < freqs::numPositions; ++p)
            {
                const int note = freqs::noteFor (b, p, false);
                freqLabels.add (String (freqs::noteName (note)) + " " + formatHz (freqs::noteToHz (note)));
            }
            const int defaultPos[4] = { 3, 5, 6, 7 };
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::bandFreq (c, b), 1 }, name + "Freq", freqLabels, defaultPos[b]));
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bandStep (c, b), 1 }, name + "+1 Step", false));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::bandGain (c, b), 1 }, name + "Gain", piecewiseRange (gainAnchors()), 0.0f,
                AudioParameterFloatAttributes()
                    .withLabel ("dB")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        const double g = applyDeadZone (v);
                        return g == 0.0 ? String ("0.0 dB") : (g > 0 ? "+" : "") + String (g, 1) + " dB";
                    })));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::bandBw (c, b), 1 }, name + "Bandwidth", piecewiseRange (bandwidthAnchors()), 1.5f,
                AudioParameterFloatAttributes()
                    .withLabel ("oct")
                    .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; })));

            if (b == 0 || b == 3)
                layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bandShelf (c, b), 1 }, name + "Shelf", false));
        }

        StringArray lcLabels { "Out" };
        for (int i = 1; i < freqs::numLowCut; ++i) lcLabels.add (String ((int) freqs::lowCutHz[i]) + " Hz");
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::lowCut (c), 1 }, chName + "Low Cut", lcLabels, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lowCutSteep (c), 1 }, chName + "Low Cut Steep", false));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::colourSrc (c), 1 }, chName + "Colour Source",
            StringArray { "Off", "Band 1", "Band 2", "Band 3", "Band 4", "Full" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::colour (c), 1 }, chName + "Colour",
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::channelIn (c), 1 }, chName + "In", true));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::link, 1 }, "Link", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return layout;
}

heron::Settings HeronEQAudioProcessor::readSettings (const juce::AudioProcessorValueTreeState& s, int c)
{
    auto get = [&] (const juce::String& id) { return s.getRawParameterValue (id)->load(); };
    heron::Settings out;
    for (int b = 0; b < 4; ++b)
    {
        auto& band = out.bands[(size_t) b];
        band.position = (int) get (ids::bandFreq (c, b));
        band.plusStep = get (ids::bandStep (c, b)) > 0.5f;
        band.gainDb = get (ids::bandGain (c, b));
        band.bandwidthOct = get (ids::bandBw (c, b));
        band.shelf = (b == 0 || b == 3) && get (ids::bandShelf (c, b)) > 0.5f;
    }
    out.lowCutIndex = (int) get (ids::lowCut (c));
    out.lowCutSteep = get (ids::lowCutSteep (c)) > 0.5f;
    out.colourSource = (ColourSource) (int) get (ids::colourSrc (c));
    out.colour = get (ids::colour (c));
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
    oversamplingOrder = osLog2For (sampleRate);          // OVERSAMPLING (right-click menu)
    preparedBlockSize = samplesPerBlock;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, oversamplingOrder, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    osRate = sampleRate * (1 << oversamplingOrder);
    channels.assign ((size_t) numCh, ChannelEQ {});
    for (auto& c : channels) c.prepare (osRate);
    // L / R: two hand-built channels never match exactly. Small fixed tolerances on the
    // right channel (frequencies +1.2 %, Q -3 %, gain +2 %) and a slightly different
    // top-end phase on each side give a touch of stereo width; mono is untouched.
    if (channels.size() > 1)
    {
        channels[0].setTolerance (1.0,   1.0,  1.0,  7000.0);
        channels[1].setTolerance (1.012, 0.97, 1.02, 7900.0);
    }
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

    const bool linked = apvts.getRawParameterValue (ids::link)->load() > 0.5f;
    const bool power = ! bypassParam->get();

    // --- Targets for each panel channel (CH1 = left, CH2 = right) ---
    struct Target { std::array<BandState, 4> bands; double colour, wet, lowCutHz; bool steep; ColourSource src; };
    std::array<Target, 2> target;
    for (int c = 0; c < 2; ++c)
    {
        const Settings s = readSettings (apvts, linked ? 0 : c);
        auto& t = target[(size_t) c];
        for (int b = 0; b < 4; ++b)
        {
            const auto& bs = s.bands[(size_t) b];
            t.bands[(size_t) b].fc = freqs::hzFor (b, juce::jlimit (0, 11, bs.position), bs.plusStep);
            t.bands[(size_t) b].gain = applyDeadZone (bs.gainDb);
            t.bands[(size_t) b].bw = bs.bandwidthOct;
            t.bands[(size_t) b].shelf = bs.shelf;
        }
        t.colour = s.colour / 10.0;
        t.lowCutHz = freqs::lowCutHz[juce::jlimit (0, freqs::numLowCut - 1, s.lowCutIndex)];
        t.steep = s.lowCutSteep;
        t.src = s.colourSource;
        // The channel IN/OUT switches are per channel, even when the controls are linked.
        t.wet = (power && apvts.getRawParameterValue (ids::channelIn (c))->load() > 0.5f) ? 1.0 : 0.0;
    }

    if (firstBlock)
    {
        for (int c = 0; c < 2; ++c)
        {
            state[(size_t) c].bands = target[(size_t) c].bands;
            state[(size_t) c].colour = target[(size_t) c].colour;
            state[(size_t) c].wet = target[(size_t) c].wet;
        }
        for (auto& c : channels) c.reset();
        firstBlock = false;
    }

    // --- Analyser: input (pre) ---
    const int numIn = buffer.getNumSamples();
    {
        const int w = anaWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numIn; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) m += buffer.getSample (ch, i);
            anaIn[(size_t) ((w + i) & (analyserSize - 1))] = m / (float) numCh;
        }
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

        for (int ch = 0; ch < numCh; ++ch)
        {
            const size_t c = (size_t) juce::jmin (ch, 1);
            auto& st = state[c];
            const auto& tg = target[c];

            std::array<ChannelEQ::BandCoeffs, 4> bc;
            for (size_t b = 0; b < 4; ++b)
            {
                auto& bs = st.bands[b];
                bs.fc *= std::pow (tg.bands[b].fc / bs.fc, a);   // glide in log-frequency
                bs.gain += (tg.bands[b].gain - bs.gain) * a;
                bs.bw *= std::pow (tg.bands[b].bw / bs.bw, a);
                bs.shelf = tg.bands[b].shelf;

                auto& k = bc[b];
                k.fc = bs.fc;
                if (bs.shelf) { k.mode = b == 0 ? SVF::Mode::LowPass1 : SVF::Mode::HighPass1; k.q = 0.70710678; }   // 6 dB/oct, BANDWIDTH unused
                else          { k.mode = SVF::Mode::BandPass; k.q = bandwidthToQ (bs.bw); }
                const double A = std::pow (10.0, bs.gain / 20.0);
                k.k = bs.gain > 0 ? A - 1.0 : 0.0;
                k.m = bs.gain < 0 ? 1.0 / A - 1.0 : 0.0;
            }
            st.colour += (tg.colour - st.colour) * a;
            const double wetStart = st.wet;
            st.wet += (tg.wet - st.wet) * a;
            if (std::abs (st.wet - tg.wet) < 1e-4) st.wet = tg.wet;

            auto& eq = channels[(size_t) ch];
            eq.update (bc, tg.lowCutHz, tg.steep, tg.src, st.colour);
            float* d = os.getChannelPointer ((size_t) ch) + start;
            for (int i = 0; i < len; ++i)
            {
                const double w = wetStart + (st.wet - wetStart) * (i + 1) / len;
                const double x = d[i];
                const double y = eq.process (x);
                d[i] = (float) (w * y + (1.0 - w) * x);
            }
        }
    }

    oversampler->processSamplesDown (block);

    // --- Analyser: output (post) ---
    {
        const int w = anaWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numIn; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) m += buffer.getSample (ch, i);
            anaOut[(size_t) ((w + i) & (analyserSize - 1))] = m / (float) numCh;
        }
        anaWrite.store ((w + numIn) & (analyserSize - 1), std::memory_order_release);
    }
}

//==============================================================================
juce::AudioProcessorEditor* HeronEQAudioProcessor::createEditor() { return new HeronEQAudioProcessorEditor (*this); }

void HeronEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

int HeronEQAudioProcessor::getOversampling() const
{
    const int f = (int) apvts.state.getProperty ("oversampling", 2);
    return f == 4 || f == 8 || f == 16 ? f : 2;
}

int HeronEQAudioProcessor::osLog2For (double sampleRate) const
{
    // the factor is chosen for 44.1 / 48 kHz; higher session rates need less, so the
    // EQ always runs at the same internal rate (and sounds the same)
    int l = juce::roundToInt (std::log2 ((double) getOversampling()));
    for (double r = sampleRate; r >= 60000.0 && l > 0; r *= 0.5) --l;
    return l;
}

void HeronEQAudioProcessor::setOversampling (int factor)
{
    apvts.state.setProperty ("oversampling", factor, nullptr);
    if (getSampleRate() > 0.0 && osLog2For (getSampleRate()) != oversamplingOrder)
    {
        // re-prepare with the audio callback held off; the new latency goes to the host
        suspendProcessing (true);
        prepareToPlay (getSampleRate(), preparedBlockSize);
        suspendProcessing (false);
    }
}

void HeronEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            setOversampling (getOversampling());      // re-prepares if the session used another factor
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HeronEQAudioProcessor(); }
