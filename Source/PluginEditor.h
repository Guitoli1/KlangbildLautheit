#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"
#include "RackStyle.h"

class LautheitAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LautheitAudioProcessorEditor (LautheitAudioProcessor&);
    ~LautheitAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void renderPanel();

    // gemeinsame Werte für die Anzeigen
    struct Values
    {
        float momentary = -200, shortTerm = -200, integrated = -200, lra = 0, tpDb = -200, maxShort = -200;
        float seconds = 0, target = -14, targetTp = -1;
        bool measuring = false;
    };

    // große Leuchtanzeige: integrierte Lautheit und Abstand zum Ziel
    struct MainReadout : public juce::Component
    {
        explicit MainReadout (VintageLookAndFeel& l, const Values& v) : lnf (l), values (v) {}
        VintageLookAndFeel& lnf; const Values& values;
        void paint (juce::Graphics&) override;
    };

    // kleine Leuchtanzeige: LRA, True Peak, max. Kurzzeit
    struct SideReadout : public juce::Component
    {
        explicit SideReadout (VintageLookAndFeel& l, const Values& v) : lnf (l), values (v) {}
        VintageLookAndFeel& lnf; const Values& values;
        void paint (juce::Graphics&) override;
    };

    // Schreiber: Kurzzeit-Lautheit der letzten 60 s auf Papier
    struct Recorder : public juce::Component
    {
        explicit Recorder (VintageLookAndFeel& l, LautheitAudioProcessor& p, const Values& v) : lnf (l), proc (p), values (v) {}
        VintageLookAndFeel& lnf; LautheitAudioProcessor& proc; const Values& values;
        void paint (juce::Graphics&) override;
    };

    LautheitAudioProcessor& proc;
    RackLookAndFeel lnf;
    Values values;

    juce::ComboBox targetBox;
    std::unique_ptr<ComboAttachment> targetAttachment;
    juce::ToggleButton transportSwitch;
    std::unique_ptr<ButtonAttachment> transportAttachment;
    juce::TextButton resetButton;

    VuMeter vuM { lnf, "M" }, vuS { lnf, "S" };
    MainReadout mainReadout { lnf, values };
    SideReadout sideReadout { lnf, values };
    Recorder recorder { lnf, proc, values };
    int frame = 0;

    static constexpr int rackW = 1280, rackH = 780;
    std::array<juce::Rectangle<int>, 4> sections;
    juce::Rectangle<int> nameplate, vuBezelM, vuBezelS, tagArea, switchLabel, resetLabel, recorderBezel;
    juce::Image panelImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LautheitAudioProcessorEditor)
};
