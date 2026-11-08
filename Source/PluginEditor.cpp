#include "PluginEditor.h"

using namespace vintage;

namespace
{
const juce::Colour amberDigit { 0xffffb347 };
const juce::Colour greenDigit { 0xff9be07a };

juce::String u8 (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); }
juce::String minus() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }

juce::String fmt (float v, int decimals, bool sign = false)
{
    if (v <= -199.0f) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x80\x93,\xe2\x80\x93"));
    const float a = std::abs (v);
    juce::String s = juce::String (a, decimals).replaceCharacter ('.', ',');
    if (v < -0.049f) return minus() + s;
    if (sign && v > 0.049f) return "+" + s;
    return s;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> b)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (2.0f, 3.0f), 10.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.25f), 0, b.getY(), gold.darker (0.6f), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, 10.0f);
    const auto in = b.reduced (6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1410), 0, in.getY(), juce::Colour (0xff0a0806), 0, in.getBottom(), false));
    g.fillRoundedRectangle (in, 6.0f);
}

void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f, juce::Colour c, juce::Justification j)
{
    g.setFont (f);
    g.setColour (c.withAlpha (0.18f));
    g.drawText (t, r.translated (0.0f, 1.0f).expanded (1.0f), j);
    g.setColour (c);
    g.drawText (t, r, j);
}
}

//==============================================================================
LautheitAudioProcessorEditor::LautheitAudioProcessorEditor (LautheitAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (int i = 0; i < (int) LautheitAudioProcessor::targets().size(); ++i)
        targetBox.addItem (juce::String (juce::CharPointer_UTF8 (LautheitAudioProcessor::targets()[(size_t) i].name)), i + 1);
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<ComboAttachment> (proc.apvts, "target", targetBox);

    transportSwitch.setButtonText ("x");
    transportSwitch.getProperties().set ("onText", "WIEDERGABE");
    transportSwitch.getProperties().set ("offText", "IMMER");
    transportSwitch.getProperties().set ("lamp", (int) lampAmber.getARGB());
    addAndMakeVisible (transportSwitch);
    transportAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "transport", transportSwitch);

    rack::styleToggle (transportSwitch);

    resetButton.setButtonText ("RESET");
    resetButton.getProperties().set ("noLamp", true);
    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetButton);

    for (auto* c : std::initializer_list<juce::Component*> { &vuM, &vuS, &mainReadout, &sideReadout, &recorder })
    {
        c->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (c);
    }

    setSize (rackW, rackH);
    startTimerHz (60);
}

LautheitAudioProcessorEditor::~LautheitAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LautheitAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    values.momentary  = proc.momentary.load();
    values.shortTerm  = proc.shortTerm.load();
    values.integrated = proc.integrated.load();
    values.lra        = proc.lra.load();
    values.maxShort   = proc.maxShort.load();
    values.seconds    = proc.secondsMeasured.load();
    values.measuring  = proc.measuring.load();
    values.tpDb       = proc.truePeakMax.load() > 0.0f ? juce::Decibels::gainToDecibels (proc.truePeakMax.load(), -200.0f) : -200.0f;
    values.target     = proc.targetLufs();
    values.targetTp   = proc.targetTp();

    // Zeiger: 0 VU entspricht dem Ziel – rechts davon zu laut, links zu leise
    auto needle = [this] (float lufs) { return lufs <= -199.0f ? 0.0f : juce::Decibels::decibelsToGain (lufs - values.target - 18.0f) / 1.1107f; };
    vuM.update (needle (values.momentary), 0.0f, dt);
    vuS.update (needle (values.shortTerm), 0.0f, dt);

    if (++frame % 6 == 0)
    {
        mainReadout.repaint();
        sideReadout.repaint();
        recorder.repaint();
    }
}

//==============================================================================
void LautheitAudioProcessorEditor::MainReadout::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    drawPanel (g, b);
    const auto in = b.reduced (18.0f, 12.0f);

    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (11.0f, 0.25f));
    g.drawText ("INTEGRIERT", in.withHeight (16.0f), juce::Justification::centredLeft);

    const int secs = (int) values.seconds;
    g.drawText (juce::String::formatted ("%d:%02d", secs / 60, secs % 60) + u8 (values.measuring ? "  \xc2\xb7  MISST" : "  \xc2\xb7  PAUSE"),
                in.withHeight (16.0f), juce::Justification::centredRight);

    const auto valueArea = in.withTrimmedTop (16.0f).withHeight (60.0f);
    const bool has = values.integrated > -199.0f;
    const float dev = values.integrated - values.target;
    const juce::Colour col = ! has ? amberDigit : (std::abs (dev) <= 1.0f ? greenDigit : amberDigit);
    const auto bigF = lnf.labelFont (58.0f, 0.02f);
    const auto valueText = fmt (values.integrated, 1);
    glowText (g, valueText, valueArea.withWidth (260.0f), bigF, col, juce::Justification::centredLeft);
    const float vw = (float) juce::GlyphArrangement::getStringWidthInt (bigF, valueText);
    g.setColour (col.withAlpha (0.7f));
    g.setFont (lnf.labelFont (16.0f, 0.15f));
    g.drawText ("LUFS", valueArea.withX (valueArea.getX() + vw + 12.0f).withWidth (60.0f).withTrimmedBottom (10.0f), juce::Justification::bottomLeft);

    // Abstand zum Ziel und Empfehlung
    const auto hint = in.withTrimmedTop (80.0f);
    g.setFont (lnf.labelFont (13.0f, 0.1f));
    juce::String text;
    if (! has)                               text = u8 ("Wiedergabe starten \xc2\xb7 die Messung l\xc3\xa4uft mit");
    else if (std::abs (dev) <= 0.5f)         text = u8 ("Ziel getroffen \xe2\x9c\x93");
    else if (dev > 0.0f)                     text = fmt (dev, 1) + u8 (" LU \xc3\xbc" "ber dem Ziel \xc2\xb7 Limiter-Gain ca. ") + minus() + fmt (dev, 1) + " dB";
    else                                     text = fmt (-dev, 1) + u8 (" LU unter dem Ziel \xc2\xb7 Limiter-Gain ca. +") + fmt (-dev, 1) + " dB";
    g.setColour (has && std::abs (dev) <= 1.0f ? greenDigit.withAlpha (0.85f) : logo.withAlpha (0.75f));
    g.drawText (text, hint, juce::Justification::centredLeft);
}

void LautheitAudioProcessorEditor::SideReadout::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    drawPanel (g, b);
    const auto in = b.reduced (16.0f, 10.0f);
    const float rowH = in.getHeight() / 3.0f;
    const bool tpOver = values.tpDb > values.targetTp + 0.05f;

    struct Row { const char* title; juce::String value; const char* unit; juce::Colour col; };
    const Row rows[] = {
        { "LRA (DYNAMIK)",  values.integrated > -199.0f ? fmt (values.lra, 1) : fmt (-200, 1), "LU",  amberDigit },
        { "TRUE PEAK MAX",  fmt (values.tpDb, 1), "dBTP", tpOver ? lampRed : amberDigit },
        { "KURZZEIT MAX",   fmt (values.maxShort, 1), "LUFS", amberDigit },
    };
    for (int i = 0; i < 3; ++i)
    {
        const auto r = in.withY (in.getY() + rowH * (float) i).withHeight (rowH);
        g.setColour (logo.withAlpha (0.55f));
        g.setFont (lnf.labelFont (10.5f, 0.22f));
        g.drawText (rows[i].title, r.withHeight (14.0f), juce::Justification::centredLeft);
        const auto vr = r.withTrimmedTop (12.0f);
        glowText (g, rows[i].value, vr.withTrimmedRight (50.0f), lnf.labelFont (30.0f, 0.02f), rows[i].col, juce::Justification::centredRight);
        g.setColour (rows[i].col.withAlpha (0.7f));
        g.setFont (lnf.labelFont (11.0f, 0.15f));
        g.drawText (rows[i].unit, vr.withTrimmedLeft (vr.getWidth() - 44.0f), juce::Justification::centredLeft);
        if (i < 2)
        {
            g.setColour (gold.withAlpha (0.22f));
            g.drawHorizontalLine ((int) (r.getBottom()), r.getX(), r.getRight());
        }
    }
}

void LautheitAudioProcessorEditor::Recorder::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    // Papier
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4ead2), 0, b.getY(), juce::Colour (0xffe6d8b8), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, 4.0f);

    const auto plot = b.reduced (44.0f, 10.0f).withTrimmedRight (-30.0f);
    const float top = values.target + 6.0f, bottom = values.target - 18.0f;
    auto yFor = [&] (float lufs) { return juce::jmap (juce::jlimit (bottom, top, lufs), bottom, top, plot.getBottom(), plot.getY()); };

    // Raster: alle 3 LU, Zeitlinien alle 5 s
    const juce::Colour gridC (0xffc9744f);
    for (float l = bottom; l <= top + 0.01f; l += 3.0f)
    {
        const float y = yFor (l);
        g.setColour (gridC.withAlpha (std::abs (l - values.target) < 0.01f ? 0.0f : 0.25f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (gridC.withAlpha (0.8f));
        g.setFont (lnf.labelFontRegular (10.0f));
        g.drawText (fmt (l, 0), juce::Rectangle<float> (b.getX() + 2.0f, y - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight);
    }
    for (int s = 0; s <= 60; s += 5)
    {
        const float x = juce::jmap ((float) s, 0.0f, 60.0f, plot.getX(), plot.getRight());
        g.setColour (gridC.withAlpha (s % 15 == 0 ? 0.35f : 0.15f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    // Ziel-Band (±1 LU) und Ziellinie
    g.setColour (juce::Colour (0x2244aa44));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yFor (values.target + 1.0f), plot.getRight(), yFor (values.target - 1.0f)));
    g.setColour (juce::Colour (0xff2e7d32).withAlpha (0.8f));
    g.drawLine (plot.getX(), yFor (values.target), plot.getRight(), yFor (values.target), 1.4f);
    g.setFont (lnf.labelFont (10.0f, 0.15f));
    g.drawText ("ZIEL", juce::Rectangle<float> (plot.getRight() - 40.0f, yFor (values.target) - 14.0f, 38.0f, 12.0f), juce::Justification::centredRight);

    // Integrierter Wert als gestrichelte Linie
    if (values.integrated > -199.0f)
    {
        juce::Path il; il.startNewSubPath (plot.getX(), yFor (values.integrated)); il.lineTo (plot.getRight(), yFor (values.integrated));
        juce::Path dashed; const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, il, dashes, 2);
        g.setColour (juce::Colour (0xff1a237e).withAlpha (0.7f));
        g.fillPath (dashed);
    }

    // Tintenlinie: Kurzzeit-Lautheit, neueste rechts
    constexpr int N = loudness::Meter::historyLen;
    const int pos = proc.historyPos.load();
    juce::Path ink; bool started = false;
    for (int i = 0; i < N; ++i)
    {
        const float v = proc.history[(size_t) ((pos + i) % N)].load();
        const float x = juce::jmap ((float) i, 0.0f, (float) (N - 1), plot.getX(), plot.getRight());
        if (v <= -199.0f) { started = false; continue; }
        const float y = yFor (v);
        if (! started) { ink.startNewSubPath (x, y); started = true; } else ink.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff8e1c1c).withAlpha (0.25f));
    g.strokePath (ink, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff8e1c1c));
    g.strokePath (ink, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Schreibstift rechts
    const float pv = proc.history[(size_t) ((pos + N - 1) % N)].load();
    if (pv > -199.0f)
    {
        const float y = yFor (pv);
        g.setColour (juce::Colour (0xff3a3632));
        g.fillEllipse (plot.getRight() - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }

    g.setColour (gridC.withAlpha (0.8f));
    g.setFont (lnf.labelFontRegular (10.0f));
    g.drawText (minus() + "60 s", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getBottom() - 14.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("JETZT", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() - 12.0f, 56.0f, 12.0f), juce::Justification::centredRight);

    // Papierrand-Schatten
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.18f), 0, b.getY(), juce::Colours::transparentBlack, 0, b.getY() + 8.0f, false));
    g.fillRect (b.withHeight (8.0f));
}

//==============================================================================
void LautheitAudioProcessorEditor::renderPanel()
{
    panelImage = rack::renderPanel (rackW, rackH, 29);
    juce::Graphics g (panelImage);
    const char* titles[] = { "PEGEL", "MESSWERTE", "INTEGRIERTE LAUTHEIT", "KURZZEIT-VERLAUF (60 S)" };
    for (int i = 0; i < 4; ++i)
        rack::drawSection (g, sections[(size_t) i], rack::u8 (titles[i]), lnf);

    rack::drawBezel (g, vuBezelM);
    rack::drawBezel (g, vuBezelS);
    rack::drawLabel (g, "MOMENTAN (400 MS)", vuBezelM.toFloat().withY ((float) vuBezelM.getBottom() + 4.0f).withHeight (14.0f), lnf, 11.0f);
    rack::drawLabel (g, "KURZZEIT (3 S)", vuBezelS.toFloat().withY ((float) vuBezelS.getBottom() + 4.0f).withHeight (14.0f), lnf, 11.0f);
    rack::drawLabel (g, rack::u8 ("0 VU = ZIEL  \xc2\xb7  RECHTS ZU LAUT  \xc2\xb7  LINKS ZU LEISE"),
                     juce::Rectangle<float> ((float) vuBezelM.getX(), (float) vuBezelM.getBottom() + 22.0f,
                                             (float) (vuBezelS.getRight() - vuBezelM.getX()), 14.0f), lnf, 10.0f);
    rack::drawBezel (g, recorderBezel, 8.0f);

    rack::drawNameplate (g, nameplate, "LAUTHEIT", "LUFS-METER", lnf);
    rack::drawLabel (g, "ZIEL", targetBox.getBounds().toFloat().withY ((float) targetBox.getY() - 16.0f).withHeight (14.0f), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawLabel (g, "MESSEN", switchLabel.toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawLabel (g, "NEU MESSEN", resetLabel.toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawLabel (g, rack::u8 ("EBU R128 \xc2\xb7 ITU BS.1770"),
                     juce::Rectangle<float> ((float) nameplate.getX(), (float) resetButton.getBottom() + 24.0f, (float) nameplate.getWidth(), 14.0f),
                     lnf, 10.0f, juce::Justification::centredLeft);
    rack::drawTypeTag (g, rack::u8 ("TYP KB-1770  \xc2\xb7  NR. 0612"), tagArea, lnf);
}

void LautheitAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (panelImage, 0, 0);
}

void LautheitAudioProcessorEditor::resized()
{
    const int top = 30, bottom = rackH - 34;
    const int r1 = top + 270, r2 = r1 + 12, r2b = r2 + 150, r3 = r2b + 12;
    sections[0] = { 72, top, 600, r1 - top };
    sections[1] = { 684, top, 346, r1 - top };
    sections[2] = { 72, r2, 958, r2b - r2 };
    sections[3] = { 72, r3, 958, bottom - r3 };

    vuBezelM = { sections[0].getX() + 14, top + 22, 282, 200 };
    vuBezelS = { sections[0].getRight() - 14 - 282, top + 22, 282, 200 };
    vuM.setBounds (vuBezelM.reduced (9));
    vuS.setBounds (vuBezelS.reduced (9));
    sideReadout.setBounds (sections[1].reduced (14).withTrimmedTop (10));
    mainReadout.setBounds (sections[2].reduced (14).withTrimmedTop (8));
    recorderBezel = sections[3].reduced (14).withTrimmedTop (8);
    recorder.setBounds (recorderBezel.reduced (7));

    const int colX = 1046, colW = rackW - 56 - 16 - colX;
    nameplate = { colX, top - 8, colW, 88 };
    targetBox.setBounds (colX, top + 112, colW, 28);
    switchLabel = { colX + 4, top + 168, colW, 14 };
    transportSwitch.setBounds (colX + 4, top + 186, 150, 40);
    resetLabel = { colX + 4, top + 252, colW, 14 };
    resetButton.setBounds (colX + 4, top + 272, 110, 36);
    tagArea = { colX - 60, rackH - 28, colW + 60, 16 };
    renderPanel();
}
