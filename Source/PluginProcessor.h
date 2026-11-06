#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessDsp.h"
#include "LimiterDsp.h"   // True-Peak-Detektor

class LautheitAudioProcessor : public juce::AudioProcessor
{
public:
    LautheitAudioProcessor();
    ~LautheitAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
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

    struct Target { const char* name; float lufs; float tpLimit; };
    static const std::vector<Target>& targets();
    float targetLufs() const;
    float targetTp() const;

    void requestReset() { resetRequested.store (true); }

    juce::AudioProcessorValueTreeState apvts;

    // Messwerte für die Oberfläche (LUFS bzw. LU, −200 = noch kein Wert)
    std::atomic<float> momentary { -200 }, shortTerm { -200 }, integrated { -200 }, lra { 0 };
    std::atomic<float> truePeakMax { 0 };        // linear, seit dem letzten Zurücksetzen
    std::atomic<float> maxMomentary { -200 }, maxShort { -200 };
    std::atomic<float> secondsMeasured { 0 };
    std::atomic<bool> measuring { false };       // läuft gerade eine Messung (Wiedergabe)?

    // Kurzzeit-Verlauf (60 s), wird alle 100 ms beschrieben
    std::array<std::atomic<float>, loudness::Meter::historyLen> history;
    std::atomic<int> historyPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    loudness::Meter meter;
    std::array<limiter::TruePeak8, 2> tp;
    float tpMaxLocal = 0;
    std::atomic<bool> resetRequested { false };
    std::atomic<float>* pTransport = nullptr;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LautheitAudioProcessor)
};
