// HostTest: loads the built Heron EQ VST3 the way a DAW would, then checks it.
//   HeronHostTest "<path to Heron EQ.vst3>"
// Needs a display for the editor check (use xvfb-run on headless Linux).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) return prm;
    return nullptr;
}

static void setParam (AudioPluginInstance& p, const String& name, float normalised)
{
    if (auto* prm = findParam (p, name)) prm->setValueNotifyingHost (normalised);
    else check (false, "parameter not found: " + name);
}

// Steady-state sine gain in dB through the plugin.
static double sineGainDb (AudioPluginInstance& p, double sr, double hz, int block, int ch = 0)
{
    p.reset();
    AudioBuffer<float> buf (2, block);
    MidiBuffer midi;
    const int total = (int) sr, measureFrom = total / 2;
    double accRe = 0, accIm = 0; int count = 0; long n = 0;
    while (n < total)
    {
        for (int i = 0; i < block; ++i)
        {
            const float v = (float) (0.25 * std::sin (2.0 * MathConstants<double>::pi * hz * (double) (n + i) / sr));
            buf.setSample (0, i, v); buf.setSample (1, i, v);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i, ++n)
            if (n >= measureFrom)
            {
                const double ph = 2.0 * MathConstants<double>::pi * hz * (double) n / sr;
                accRe += buf.getSample (ch, i) * std::cos (ph);
                accIm -= buf.getSample (ch, i) * std::sin (ph);
                ++count;
            }
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (accRe * accRe + accIm * accIm) / count / 0.25);
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: HeronHostTest <plugin.vst3>\n"); return 2; }
    ScopedJuceInitialiser_GUI gui;

    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);

    OwnedArray<PluginDescription> descs;
    for (auto* f : fm.getFormats())
        if (f->getName() == "VST3")
            f->findAllTypesForFile (descs, File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
    check (descs.size() == 1, "VST3 found and scanned");
    if (descs.isEmpty()) return 1;
    std::printf ("      %s by %s, category %s\n", descs[0]->name.toRawUTF8(), descs[0]->manufacturerName.toRawUTF8(), descs[0]->category.toRawUTF8());

    String err;
    const double sr = 48000.0; const int block = 512;
    auto plugin = fm.createPluginInstance (*descs[0], sr, block, err);
    check (plugin != nullptr, "instantiated " + err);
    if (! plugin) return 1;

    plugin->setPlayConfigDetails (2, 2, sr, block);
    plugin->prepareToPlay (sr, block);
    std::printf ("      latency %d samples, %d parameters\n", plugin->getLatencySamples(), plugin->getParameters().size());
    check (plugin->getLatencySamples() >= 0 && plugin->getLatencySamples() < 64, "latency reported");
    check (plugin->getBypassParameter() != nullptr, "host bypass parameter exposed");

    // Flat by default.
    const double flat = sineGainDb (*plugin, sr, 1000.0, block);
    check (std::abs (flat) < 0.1, "flat at default settings (" + String (flat, 3) + " dB)");

    // Band 3 default position is A#6 (1864.7 Hz); full boost should be +12 dB.
    setParam (*plugin, "CH1 Band 3 Gain", 1.0f);
    const double boost = sineGainDb (*plugin, sr, 1864.655, block);
    check (std::abs (boost - 12.0) < 0.3, "band 3 +12 dB at 1.86 kHz (" + String (boost, 2) + " dB)");
    setParam (*plugin, "CH1 Band 3 Gain", 0.0f);
    const double cut = sineGainDb (*plugin, sr, 1864.655, block);
    check (std::abs (cut + 12.0) < 0.3, "band 3 -12 dB at 1.86 kHz (" + String (cut, 2) + " dB)");

    // Linked (default): CH1 controls drive both channels.
    const double linkedRight = sineGainDb (*plugin, sr, 1864.655, block, 1);
    check (std::abs (linkedRight + 12.0) < 0.3, "linked: right channel follows CH1 (" + String (linkedRight, 2) + " dB)");
    // Unlinked: the right channel uses CH2's (flat) settings.
    setParam (*plugin, "Link", 0.0f);
    const double unlinkedRight = sineGainDb (*plugin, sr, 1864.655, block, 1);
    check (std::abs (unlinkedRight) < 0.1, "unlinked: right channel uses CH2 (" + String (unlinkedRight, 3) + " dB)");
    setParam (*plugin, "CH2 In", 0.0f);
    setParam (*plugin, "CH2 Band 3 Gain", 1.0f);
    const double ch2Out = sineGainDb (*plugin, sr, 1864.655, block, 1);
    check (std::abs (ch2Out) < 0.1, "CH2 IN/OUT switch set to OUT (" + String (ch2Out, 3) + " dB)");
    setParam (*plugin, "CH2 In", 1.0f);
    const double ch2In = sineGainDb (*plugin, sr, 1864.655, block, 1);
    check (std::abs (ch2In - 12.0) < 0.3, "CH2 boost when unlinked (" + String (ch2In, 2) + " dB)");
    setParam (*plugin, "Link", 1.0f);

    // Bypass returns to unity.
    plugin->getBypassParameter()->setValueNotifyingHost (1.0f);
    const double byp = sineGainDb (*plugin, sr, 1864.655, block);
    check (std::abs (byp) < 0.1, "bypass is unity (" + String (byp, 3) + " dB)");
    plugin->getBypassParameter()->setValueNotifyingHost (0.0f);

    // Busy settings, random noise, odd block sizes, automation mid-stream: output must stay finite.
    setParam (*plugin, "CH1 Band 1 Shelf", 1.0f);
    setParam (*plugin, "CH1 Band 1 Gain", 0.8f);
    setParam (*plugin, "CH1 Band 2 Gain", 0.3f);
    setParam (*plugin, "CH1 Band 4 Gain", 0.85f);
    setParam (*plugin, "CH1 Low Cut", 0.35f);
    setParam (*plugin, "CH1 Colour Source", 1.0f);
    setParam (*plugin, "CH1 Colour", 0.8f);
    {
        Random rng (7);
        bool finite = true; float peak = 0;
        MidiBuffer midi;
        for (int it = 0; it < 400; ++it)
        {
            const int n = 1 + rng.nextInt (block);
            AudioBuffer<float> buf (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
            if (it % 25 == 0) setParam (*plugin, "CH1 Band 2 Freq", rng.nextFloat());
            if (it % 30 == 0) setParam (*plugin, "CH1 Band 3 Bandwidth", rng.nextFloat());
            plugin->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    finite &= std::isfinite (v);
                    peak = jmax (peak, std::abs (v));
                }
        }
        check (finite && peak < 20.0f, "stress run finite (peak " + String (peak, 2) + ")");
    }

    // State round trip.
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        const float before = findParam (*plugin, "CH1 Band 4 Gain")->getValue();
        setParam (*plugin, "CH1 Band 4 Gain", 0.1f);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        const float after = findParam (*plugin, "CH1 Band 4 Gain")->getValue();
        check (std::abs (before - after) < 1e-4f, "state save / restore");
    }

    // Editor opens and closes cleanly.
    {
        std::unique_ptr<AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 0, "editor created");
        if (ed) plugin->editorBeingDeleted (ed.get());
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf (failures == 0 ? "\nAll host tests passed.\n" : "\n%d host test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
