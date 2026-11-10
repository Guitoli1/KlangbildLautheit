#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ScaledEditor.h"

const std::vector<LautheitAudioProcessor::Target>& LautheitAudioProcessor::targets()
{
    static const std::vector<Target> t = {
        { "Spotify \xc2\xb7 YouTube \xc2\xb7 Tidal (\xe2\x88\x92" "14 LUFS)", -14.0f, -1.0f },
        { "Apple Music \xc2\xb7 Podcast (\xe2\x88\x92" "16 LUFS)",        -16.0f, -1.0f },
        { "Deezer (\xe2\x88\x92" "15 LUFS)",                               -15.0f, -1.0f },
        { "Amazon Music (\xe2\x88\x92" "14 LUFS, \xe2\x88\x92" "2 dBTP)",   -14.0f, -2.0f },
        { "Rundfunk EBU R128 (\xe2\x88\x92" "23 LUFS)",                    -23.0f, -1.0f },
        { "CD / laut (\xe2\x88\x92" "9 LUFS)",                             -9.0f,  -0.3f },
    };
    return t;
}

juce::AudioProcessorValueTreeState::ParameterLayout LautheitAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    juce::StringArray names;
    for (auto& t : targets()) names.add (juce::String (juce::CharPointer_UTF8 (t.name)));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "target", 1 }, "Ziel", names, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "transport", 1 }, "Nur bei Wiedergabe messen", true));
    return l;
}

float LautheitAudioProcessor::targetLufs() const
{
    const int i = juce::jlimit (0, (int) targets().size() - 1, (int) apvts.getRawParameterValue ("target")->load());
    return targets()[(size_t) i].lufs;
}

float LautheitAudioProcessor::targetTp() const
{
    const int i = juce::jlimit (0, (int) targets().size() - 1, (int) apvts.getRawParameterValue ("target")->load());
    return targets()[(size_t) i].tpLimit;
}

LautheitAudioProcessor::LautheitAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildLautheit", createLayout())
{
    pTransport = apvts.getRawParameterValue ("transport");
    for (auto& h : history) h.store (-200.0f);
}

bool LautheitAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void LautheitAudioProcessor::prepareToPlay (double sampleRate, int)
{
    meter.prepare (sampleRate, juce::jlimit (1, 2, getTotalNumInputChannels()));
    for (auto& t : tp) t.reset();
    tpMaxLocal = 0;
    // Die Messung läuft weiter, wenn der Host nur neu vorbereitet – Werte bleiben stehen.
}

void LautheitAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Das Signal wird nicht verändert – nur gemessen.
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (nch == 0)
        return;

    if (resetRequested.exchange (false))
    {
        meter.resetIntegrated();
        tpMaxLocal = 0;
        for (auto& h : history) h.store (-200.0f);
    }

    // Läuft der Host? (ohne Transport-Info, z. B. Standalone: immer messen)
    bool playing = true;
    if (pTransport->load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                playing = pos->getIsPlaying();

    // Wiedergabe startet nach Stillstand am Songanfang: automatisch neu messen
    if (playing && ! wasPlaying && pTransport->load() > 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getTimeInSeconds(); t.hasValue() && *t <= 0.05)
                {
                    meter.resetIntegrated();
                    tpMaxLocal = 0;
                }
    wasPlaying = playing;
    measuring.store (playing);

    const float* L = buffer.getReadPointer (0);
    const float* R = nch > 1 ? buffer.getReadPointer (1) : L;
    for (int i = 0; i < n; ++i)
    {
        if (playing)
        {
            tpMaxLocal = juce::jmax (tpMaxLocal, tp[0].process (L[i]));
            if (nch > 1) tpMaxLocal = juce::jmax (tpMaxLocal, tp[1].process (R[i]));
        }
        if (meter.process (L[i], R[i], playing))
        {
            const int p = historyPos.load();
            history[(size_t) p].store ((float) meter.shortTerm);
            historyPos.store ((p + 1) % loudness::Meter::historyLen);
        }
    }

    momentary.store ((float) meter.momentary);
    shortTerm.store ((float) meter.shortTerm);
    integrated.store ((float) meter.integrated);
    lra.store ((float) meter.lra);
    maxMomentary.store ((float) meter.maxMomentary);
    maxShort.store ((float) meter.maxShort);
    secondsMeasured.store ((float) meter.stepsGated * 0.1f);
    truePeakMax.store (tpMaxLocal);
}

void LautheitAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LautheitAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
}

juce::AudioProcessorEditor* LautheitAudioProcessor::createEditor() { return new kb::ScaledEditor (*this, new LautheitAudioProcessorEditor (*this)); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LautheitAudioProcessor(); }
#endif
