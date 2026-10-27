/*
  ==============================================================================
    PluginEditor.cpp — MAX 五實驗對照（繁中 UTF-8）
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String utf8 (const char* s)
{
    return juce::String (juce::CharPointer_UTF8 (s));
}

void styleSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
}

void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setColour (juce::Label::textColourId, juce::Colour (0xffe8e4dc));
}

void setParamActive (juce::Slider& s, juce::Label& l, bool active)
{
    s.setEnabled (active);
    s.setAlpha (active ? 1.0f : 0.35f);
    l.setAlpha (active ? 1.0f : 0.35f);
    l.setColour (juce::Label::textColourId,
                 active ? juce::Colour (0xfff0c96a) : juce::Colour (0xff7a7670));
}

struct ActiveMask
{
    bool shift = false, shiftreel = false, shiftimage = false;
    bool mulreel = false, mulimage = false, offsetBins = false;
};

ActiveMask maskForExperiment (int exp)
{
    ActiveMask m;
    switch (exp)
    {
        case 0: m.mulreel = m.mulimage = true; break;
        case 1: m.shift = true; break;
        case 2: m.shiftreel = m.shiftimage = true; break;
        case 3: m.offsetBins = true; break;
        case 4: m.shift = true; break;
        default: break;
    }
    return m;
}
}

NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      apvts (p.getAPVTS())
{
    titleLabel.setText (utf8 ("STFT 頻譜實驗器 — MAX 五實驗"), juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (17.0f));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xfff2eee6));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    pipelineLabel.setFont (juce::FontOptions (11.5f));
    pipelineLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8b2a8));
    pipelineLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (pipelineLabel);

    styleLabel (fftSizeLabel, "fftSize");
    addAndMakeVisible (fftSizeLabel);
    fftSizeBox.addItemList (juce::StringArray { "512", "1024", "2048", "4096" }, 1);
    addAndMakeVisible (fftSizeBox);
    fftSizeAtt = std::make_unique<ComboAttachment> (apvts, "fftSize", fftSizeBox);

    styleLabel (experimentLabel, "experiment");
    addAndMakeVisible (experimentLabel);
    experimentBox.addItemList (
        juce::StringArray { "0_mul", "1_pitch", "2_split", "3_add", "4_write" }, 1);
    addAndMakeVisible (experimentBox);
    experimentAtt = std::make_unique<ComboAttachment> (apvts, "experiment", experimentBox);

    auto addSlider = [this] (juce::Slider& s, juce::Label& l, const char* name,
                             std::unique_ptr<SliderAttachment>& att, const char* id)
    {
        styleLabel (l, name);
        styleSlider (s);
        s.setColour (juce::Slider::thumbColourId, juce::Colour (0xffc4a35a));
        s.setColour (juce::Slider::trackColourId, juce::Colour (0xff3a3834));
        addAndMakeVisible (l);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };

    addSlider (shiftSlider,      shiftLabel,      "shift",      shiftAtt,      "shift");
    addSlider (shiftreelSlider,  shiftreelLabel,  "shiftreel",  shiftreelAtt,  "shiftreel");
    addSlider (shiftimageSlider, shiftimageLabel, "shiftimage", shiftimageAtt, "shiftimage");
    addSlider (mulreelSlider,    mulreelLabel,    "mulreel",    mulreelAtt,    "mulreel");
    addSlider (mulimageSlider,   mulimageLabel,   "mulimage",   mulimageAtt,   "mulimage");
    addSlider (offsetBinsSlider, offsetBinsLabel, "offsetBins", offsetBinsAtt, "offsetBins");
    addSlider (mixSlider, mixLabel, "Dry/Wet", mixAtt, "mix");
    addSlider (outputSlider, outputLabel, "Output dB", outputAtt, "output");

    helpLabel.setFont (juce::FontOptions (11.0f));
    helpLabel.setColour (juce::Label::textColourId, juce::Colour (0xffddd8ce));
    helpLabel.setJustificationType (juce::Justification::topLeft);
    helpLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (helpLabel);

    apvts.addParameterListener ("experiment", this);
    apvts.addParameterListener ("fftSize", this);
    updatePipelineText();
    updateHelpText();

    setSize (720, 900);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    apvts.removeParameterListener ("experiment", this);
    apvts.removeParameterListener ("fftSize", this);
}

void NewProjectAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "experiment" || parameterID == "fftSize")
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NewProjectAudioProcessorEditor> (this)]
        {
            if (safe != nullptr)
            {
                safe->updatePipelineText();
                safe->updateHelpText();
            }
        });
}

void NewProjectAudioProcessorEditor::updatePipelineText()
{
    const int choice = (int) apvts.getRawParameterValue ("fftSize")->load();
    const int size = 512 << choice;
    const int hop = size / 4;
    const double hz = audioProcessor.getHzPerBin();

    juce::String t;
    t << utf8 ("流程：輸入 -> ") << size << utf8 ("點 -> Hann -> FFT -> experiment(MAX 0~4) -> IFFT -> OLA -> Dry/Wet -> Output。hop=")
      << hop << utf8 ("。亮起=有用。1 bin≈") << juce::String (hz, 3) << utf8 (" Hz。\n")
      << utf8 ("Dry/Wet：0=只乾聲（已延遲 ") << size << utf8 (" 點，對齊濕聲），1=全濕。Output：-24..+24 dB，0=不變。\n")
      << utf8 ("範圍：shift類 0.01~16｜mul類 0~16｜offsetBins ±2048。太大後果見下方。");
    pipelineLabel.setText (t, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::updateHelpText()
{
    const int exp = (int) apvts.getRawParameterValue ("experiment")->load();
    const double hz = audioProcessor.getHzPerBin();

    const auto m = maskForExperiment (exp);
    setParamActive (shiftSlider,      shiftLabel,      m.shift);
    setParamActive (shiftreelSlider,  shiftreelLabel,  m.shiftreel);
    setParamActive (shiftimageSlider, shiftimageLabel, m.shiftimage);
    setParamActive (mulreelSlider,    mulreelLabel,    m.mulreel);
    setParamActive (mulimageSlider,   mulimageLabel,   m.mulimage);
    setParamActive (offsetBinsSlider, offsetBinsLabel, m.offsetBins);

    auto line = [] (const char* a, const char* b, const char* c) -> juce::String
    {
        juce::String s;
        s << utf8 (a) << utf8 (" | ") << utf8 (b) << utf8 (" | ") << utf8 (c) << "\n";
        return s;
    };

    juce::String head;
    head << utf8 ("【數值太大會怎樣】\n")
         << utf8 ("shift類 >>1：頻譜嚴重摺疊、金屬聲、空洞。<<1：悶糊、擠向低頻。\n")
         << utf8 ("mulreel >>1：爆音削波。mulimage >>1：相位碎裂、忽大忽小。\n")
         << utf8 ("offsetBins 很大：平移循環疊回。\n\n")
         << utf8 ("參數 | 有用? | 動了會怎樣\n")
         << utf8 ("-----|------|---------------------------\n");

    juce::String t;
    auto addRow = [&] (bool on, const char* name, const char* desc)
    {
        t << line (name, on ? "有用" : "無用", on ? desc : "轉了不變聲");
    };

    // 對照你的 MAX gen.codebox~（offset → 這裡叫 offsetBins）
    auto genBlock = [&] (const char* code)
    {
        t << utf8 ("\n【對應 MAX gen.codebox~】\n") << utf8 (code) << "\n";
    };

    switch (exp)
    {
        case 0:
            t << utf8 ("【0_mul】振幅×mulreel、相位×mulimage（不換 bin）\n")
              << utf8 ("≈ MAX experiment==0｜流行效果：Gain / Utility\n") << head;
            addRow (false, "shift", "");
            addRow (false, "shiftreel", "");
            addRow (false, "shiftimage", "");
            addRow (true,  "mulreel", "振幅x；>4易爆｜=0靜音｜建議先靠近1");
            addRow (true,  "mulimage", "相位x；很大→碎裂｜建議先靠近1");
            addRow (false, "offsetBins", "");
            genBlock (
                "if (experiment == 0) {\n"
                "  // 0 純乘法：不動格子\n"
                "  out1 = reel * mulreel;\n"
                "  out2 = image * mulimage;\n"
                "}");
            break;

        case 1:
            t << utf8 ("【1_pitch】輸出 i 讀輸入 i×shift（linear peek）\n")
              << utf8 ("≈ MAX experiment==1｜流行效果：Pitch Shift（粗暴版）\n") << head;
            addRow (true,  "shift", ">1壓頻譜；<1拉頻譜；>4極金屬摺疊");
            addRow (false, "shiftreel", "");
            addRow (false, "shiftimage", "");
            addRow (false, "mulreel", "");
            addRow (false, "mulimage", "");
            addRow (false, "offsetBins", "");
            genBlock (
                "else if (experiment == 1) {\n"
                "  // 1 標準 pitch：寫 idx，讀 idx×shift\n"
                "  poke(reeldata, reel, idx);\n"
                "  poke(imagedata, image, idx);\n"
                "  readidx = wrap(idx * shift, 0, 4095);\n"
                "  out1 = peek(reeldata, readidx, 0, interp=\"linear\");\n"
                "  out2 = peek(imagedata, readidx, 0, interp=\"linear\");\n"
                "}");
            break;

        case 2:
            t << utf8 ("【2_split】振幅讀 shiftreel、相位讀 shiftimage\n")
              << utf8 ("≈ MAX experiment==2｜流行效果：Vocoder / Spectral Split\n") << head;
            addRow (false, "shift", "");
            addRow (true,  "shiftreel", "振幅讀取倍率；太大→響度來源錯亂");
            addRow (true,  "shiftimage", "相位讀取倍率；太大→相位錯位");
            addRow (false, "mulreel", "");
            addRow (false, "mulimage", "");
            addRow (false, "offsetBins", "");
            genBlock (
                "else if (experiment == 2) {\n"
                "  // 2 reel / image 分開移調\n"
                "  poke(reeldata, reel, idx);\n"
                "  poke(imagedata, image, idx);\n"
                "  readR = wrap(idx * shiftreel, 0, 4095);\n"
                "  readI = wrap(idx * shiftimage, 0, 4095);\n"
                "  out1 = peek(reeldata, readR, 0, interp=\"linear\");\n"
                "  out2 = peek(imagedata, readI, 0, interp=\"linear\");\n"
                "}");
            break;

        case 3:
            t << utf8 ("【3_add】輸出 i 讀輸入 (i+offsetBins)\n")
              << utf8 ("≈ MAX experiment==3｜offsetBins = 你的 Param offset｜Frequency Shifter\n")
              << utf8 ("1 bin≈") << juce::String (hz, 3) << utf8 (" Hz\n") << head;
            addRow (false, "shift", "");
            addRow (false, "shiftreel", "");
            addRow (false, "shiftimage", "");
            addRow (false, "mulreel", "");
            addRow (false, "mulimage", "");
            addRow (true,  "offsetBins", "平移幾個 bin；很大會循環疊頻");
            genBlock (
                "else if (experiment == 3) {\n"
                "  // 3 idx 加法偏移（Max: offset → 這裡 offsetBins）\n"
                "  poke(reeldata, reel, idx);\n"
                "  poke(imagedata, image, idx);\n"
                "  readidx = wrap(idx + offset, 0, 4095);\n"
                "  out1 = peek(reeldata, readidx, 0, interp=\"linear\");\n"
                "  out2 = peek(imagedata, readidx, 0, interp=\"linear\");\n"
                "}");
            break;

        case 4:
            t << utf8 ("【4_write】輸入 i 寫到 i×shift；多源同格複數相加\n")
              << utf8 ("≈ MAX experiment==4（你註解：原5）｜Spectral Scatter / Smear\n") << head;
            addRow (true,  "shift", "寫入倍率；很大→擠成一團、超空洞");
            addRow (false, "shiftreel", "");
            addRow (false, "shiftimage", "");
            addRow (false, "mulreel", "");
            addRow (false, "mulimage", "");
            addRow (false, "offsetBins", "");
            genBlock (
                "else {\n"
                "  // 4＝原本的5：寫到 idx×shift，從 idx 讀\n"
                "  writeidx = wrap(idx * shift, 0, 4095);\n"
                "  poke(reeldata, reel, writeidx);\n"
                "  poke(imagedata, image, writeidx);\n"
                "  out1 = peek(reeldata, idx, 0, interp=\"linear\");\n"
                "  out2 = peek(imagedata, idx, 0, interp=\"linear\");\n"
                "}");
            break;

        default:
            t = utf8 ("未知 experiment");
            break;
    }

    helpLabel.setText (t, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1b19));
    g.setColour (juce::Colour (0xff2a2824));
    g.fillRoundedRectangle (getLocalBounds().reduced (10).toFloat(), 8.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    titleLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    pipelineLabel.setBounds (r.removeFromTop (40));
    r.removeFromTop (4);

    auto rowF = r.removeFromTop (26);
    fftSizeLabel.setBounds (rowF.removeFromLeft (100));
    fftSizeBox.setBounds (rowF);
    r.removeFromTop (3);

    auto rowE = r.removeFromTop (26);
    experimentLabel.setBounds (rowE.removeFromLeft (100));
    experimentBox.setBounds (rowE);
    r.removeFromTop (6);

    auto place = [&r] (juce::Label& l, juce::Slider& s)
    {
        auto row2 = r.removeFromTop (24);
        l.setBounds (row2.removeFromLeft (100));
        s.setBounds (row2);
        r.removeFromTop (2);
    };

    place (shiftLabel, shiftSlider);
    place (shiftreelLabel, shiftreelSlider);
    place (shiftimageLabel, shiftimageSlider);
    place (mulreelLabel, mulreelSlider);
    place (mulimageLabel, mulimageSlider);
    place (offsetBinsLabel, offsetBinsSlider);
    place (mixLabel, mixSlider);
    place (outputLabel, outputSlider);

    r.removeFromTop (6);
    helpLabel.setBounds (r);
}
