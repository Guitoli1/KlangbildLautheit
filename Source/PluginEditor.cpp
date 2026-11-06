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

    targetBox.getProperties().set ("plate", true);
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

    resetButton.setButtonText ("RESET");
    resetButton.getProperties().set ("noLamp", true);
    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetButton);

    for (auto* c : std::initializer_list<juce::Component*> { &vuM, &vuS, &mainReadout, &sideReadout, &recorder })
    {
        c->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (c);
    }

    setSize (1100, 720);
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
void LautheitAudioProcessorEditor::renderTextures()
{
    tolexImage = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    {
        juce::Image::BitmapData data (tolexImage, juce::Image::BitmapData::writeOnly);
        juce::Random rnd (7);
        for (int y = 0; y < tolexImage.getHeight(); ++y)
            for (int x = 0; x < tolexImage.getWidth(); ++x)
            {
                const int weave = ((x + y) % 4 == 0 || (x - y + 4096) % 4 == 0) ? -5 : 0;
                const int n = rnd.nextInt (13) - 6 + weave;
                data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, 34 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 28 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 24 + n)));
            }
    }
    if (faceplate.isEmpty())
        return;
    faceplateImage = juce::Image (juce::Image::ARGB, faceplate.getWidth(), faceplate.getHeight(), true);
    juce::Graphics g (faceplateImage);
    const auto r = faceplateImage.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (cream.brighter (0.06f), 0, 0, creamDark, 0, r.getHeight(), false));
    g.fillRoundedRectangle (r, 6.0f);
    juce::Random rnd (3);
    for (int y = 0; y < faceplate.getHeight(); ++y)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colour (0xff6b5534)).withAlpha (0.015f + rnd.nextFloat() * 0.035f));
        g.drawHorizontalLine (y, 0.0f, r.getWidth());
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colour (0x30694a1c), 0, 0, true));
    g.fillRoundedRectangle (r, 6.0f);
}

void LautheitAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                                 juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawFittedText (text, r.translated (0.0f, 1.0f).toNearestInt(), j, 2, 1.0f);
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt(), j, 2, 1.0f);
}

void LautheitAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Logo
    const auto scriptF = lnf.scriptFont (64.0f);
    const auto logoArea = juce::Rectangle<float> (40.0f, 8.0f, 400.0f, 84.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", logoArea.translated (2.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", logoArea, juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (26.0f, 0.35f));
    g.drawText ("LAUTHEIT", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 220.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("LUFS-METER \xc2\xb7 EBU R128 \xc2\xb7 ITU BS.1770"), juce::Rectangle<float> (44.0f, 90.0f, 420.0f, 16.0f), juce::Justification::centredLeft);

    const auto ta = targetArea.toFloat();
    g.setColour (logo.withAlpha (0.6f));
    g.setFont (lnf.labelFont (10.0f, 0.3f));
    g.drawText ("ZIEL", ta.withY (ta.getY() - 15.0f).withHeight (13.0f), juce::Justification::centredLeft);

    // Frontplatte
    const auto fp = faceplate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (fp.expanded (4.0f).translated (0, 2.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.3f), 0, fp.getY(), gold.darker (0.6f), 0, fp.getBottom(), false));
    g.fillRoundedRectangle (fp.expanded (3.0f), 8.0f);
    g.drawImageAt (faceplateImage, faceplate.getX(), faceplate.getY());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (fp, 6.0f, 1.0f);
    for (auto [x, y, a] : { std::tuple { fp.getX() + 12.0f, fp.getY() + 12.0f, 0.4f }, std::tuple { fp.getRight() - 12.0f, fp.getY() + 12.0f, 1.3f },
                            std::tuple { fp.getX() + 12.0f, fp.getBottom() - 12.0f, 2.2f }, std::tuple { fp.getRight() - 12.0f, fp.getBottom() - 12.0f, 0.9f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 5.0f, a);

    // VU-Rahmen mit Titeln
    const juce::String titles[] = { "MOMENTAN (400 MS)", "KURZZEIT (3 S)" };
    int k = 0;
    for (auto vbi : { vuBezelM, vuBezelS })
    {
        const auto vb = vbi.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (vb.translated (2.0f, 3.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, vb.getY(), juce::Colour (0xff120f0c), 0, vb.getBottom(), false));
        g.fillRoundedRectangle (vb, 12.0f);
        drawEngraved (g, titles[k++], vb.withY (vb.getBottom() + 4.0f).withHeight (20.0f), lnf.labelFont (13.0f, 0.18f), juce::Justification::centred);
    }
    drawEngraved (g, juce::CharPointer_UTF8 ("0 VU = ZIEL \xc2\xb7 RECHTS ZU LAUT \xc2\xb7 LINKS ZU LEISE"),
                  juce::Rectangle<float> ((float) vuBezelM.getX(), (float) vuBezelM.getBottom() + 24.0f, (float) (vuBezelS.getRight() - vuBezelM.getX()), 16.0f),
                  lnf.labelFontRegular (11.0f), juce::Justification::centred);

    // Schreiber-Rahmen
    const auto rb = recorderBezel.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (rb.translated (2.0f, 3.0f), 10.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, rb.getY(), juce::Colour (0xff120f0c), 0, rb.getBottom(), false));
    g.fillRoundedRectangle (rb, 10.0f);
    drawEngraved (g, "KURZZEIT-VERLAUF (60 S)", rb.withY (rb.getY() - 20.0f).withHeight (16.0f).withWidth (300.0f), lnf.labelFont (12.0f, 0.18f), juce::Justification::centredLeft);

    drawEngraved (g, "MESSEN", switchLabel.toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);
    drawEngraved (g, "NEU MESSEN", resetLabel.toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);

    const auto tag = tagArea.toFloat();
    g.setColour (ink.withAlpha (0.85f));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (gold);
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("MODELL KB-1770", tag, juce::Justification::centred);
}

void LautheitAudioProcessorEditor::resized()
{
    const int W = getWidth();
    targetArea = { 44, 128, 360, 32 };
    targetBox.setBounds (targetArea);
    mainReadout.setBounds (W - 500, 22, 470, 130);

    faceplate = { 24, 178, W - 48, getHeight() - 178 - 22 };
    const auto in = faceplate.reduced (30, 22);

    // obere Reihe: VU M | VU S | Seitenanzeige
    vuBezelM = { in.getX(), in.getY() + 4, 282, 214 };
    vuBezelS = { in.getX() + 300, in.getY() + 4, 282, 214 };
    vuM.setBounds (vuBezelM.reduced (9));
    vuS.setBounds (vuBezelS.reduced (9));
    sideReadout.setBounds (in.getX() + 610, in.getY(), 230, 222);

    const int ctrlX = in.getX() + 860, ctrlW = in.getRight() - ctrlX;
    switchLabel = { ctrlX, in.getY() + 6, ctrlW, 16 };
    transportSwitch.setBounds (ctrlX + (ctrlW - 130) / 2, in.getY() + 28, 130, 40);
    resetLabel = { ctrlX, in.getY() + 92, ctrlW, 16 };
    resetButton.setBounds (ctrlX + (ctrlW - 100) / 2, in.getY() + 114, 100, 38);
    tagArea = { ctrlX + (ctrlW - 120) / 2, in.getY() + 186, 120, 24 };

    // unten: Schreiber
    recorderBezel = { in.getX(), in.getY() + 286, in.getWidth(), in.getBottom() - (in.getY() + 286) };
    recorder.setBounds (recorderBezel.reduced (8));

    renderTextures();
}
