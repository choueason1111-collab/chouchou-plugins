/*
  ==============================================================================
    PluginEditor.cpp — Spectral Lab IA（防呆：無用旋鈕鎖定）
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

struct ExpInfo
{
    const char* title;
    const char* popular;
    const char* knobs;
    const char* danger;
};

ExpInfo infoFor (int exp)
{
    switch (exp)
    {
        case 0: return { "0_mul", "Gain / Utility", "mulreel,mulimage", "mulreel过大爆音；mulimage过大相位碎裂" };
        case 1: return { "1_pitch", "Pitch Shift（粗暴）", "shift", "shift>>1 金屬摺疊；<<1 悶糊" };
        case 2: return { "2_split", "Vocoder / Spectral Split", "shiftreel,shiftimage", "兩者差很大時音色怪異" };
        case 3: return { "3_fshift", "Frequency Shifter", "offsetBins", "很大会循環疊頻" };
        case 4: return { "4_scatter", "Spectral Smear", "shift", "很大→能量擠成一團、空洞" };
        case 5: return { "5_formant", "Formant Shift", "shiftreel,mulreel", "shiftreel极端→娃娃/怪獸聲" };
        case 6: return { "6_pitchForm", "Pitch+Formant 分开", "shift,shiftreel,mulreel", "两者都很大極不自然" };
        case 7: return { "7_mirror", "Spectral Mirror", "offsetBins,mul*", "高低頻對調後再平移" };
        case 8: return { "8_gate", "Spectral Gate", "mulreel,mulimage", "mulreel很大→近乎靜音" };
        case 9: return { "9_lpf", "Low-Pass", "offsetBins,mul*", "cutoff太小只剩低音" };
        case 10: return { "10_hpf", "High-Pass", "offsetBins,mul*", "cutoff太大幾乎砍光" };
        case 11: return { "11_bpf", "Band-Pass / Telephone", "offsetBins,shift,mul*", "shift很大≈接近全通" };
        case 12: return { "12_notch", "Notch / EQ Dip", "offsetBins,shift,mul*", "shift很大挖掉大半頻譜" };
        case 13: return { "13_blur", "Spectral Blur", "offsetBins,mul*", "半徑大→糊成一團" };
        case 14: return { "14_robot", "Robotize", "mulreel,mulimage", "mulimage大→硬機器聲" };
        case 15: return { "15_whisper", "Whisperize", "mulreel,mulimage", "mulreel大仍可能吵" };
        case 16: return { "16_harmonic", "Harmonic Filter", "offsetBins,mul*", "步进大→只剩少數峰" };
        case 17: return { "17_pitchGain", "Pitch+Gain", "shift,mul*", "同 pitch，另加增益爆音風險" };
        case 18: return { "18_fshiftGain", "FreqShift+Level", "offsetBins,mul*", "平移过大循环" };
        case 19: return { "19_magShift", "Formant(用shift)", "shift,mulreel", "只搬振幅" };
        case 20: return { "20_phaseShift", "相位扰乱", "shiftimage,mulreel", "相位錯位梳狀抵消" };
        case 21: return { "21_comb", "Comb / 靜止 Flanger", "offsetBins,mul*", "週期很大齒很疏" };
        case 22: return { "22_binQuant", "Lo-fi 階梯化", "offsetBins,mul*", "步进大極度Lo-fi" };
        case 23: return { "23_octDown", "八度下（粗）", "mul*", "读 idx*2，近似降八度音色" };
        case 24: return { "24_octUp", "八度上（粗）", "mul*", "读 idx/2，近似升八度" };
        case 25: return { "25_contrast", "頻譜對比度", "shiftreel(=gamma),mul*", "shiftreel當指數，很大更刺" };
        case 26: return { "26_peaks", "只留局部峰", "shift(敏感度),mul*", "像噪音抑制留共鳴" };
        case 27: return { "27_valleys", "只留谷值", "mul*", "奇怪的反向音色" };
        case 28: return { "28_shepard", "Shepard式旋轉", "offsetBins,shift,mul*", "持續移動感" };
        case 29: return { "29_glitch", "隨機跳bin", "offsetBins,shift,mul*", "很大很亂很吵" };
        case 30: return { "30_stereoDiff", "左右不同變調", "shift(L),shiftreel(R),mul*", "加寬/分離" };
        case 31: return { "31_phaseCancel", "奇偶相位翻轉", "mul*", "薄、梳狀" };
        case 32: return { "32_envelopeFollow", "振幅包絡平滑", "shift(半徑),mul*", "糊化響度輪廓" };
        case 33: return { "33_brickwall", "振幅天花板", "mulreel=上限", "壓動態" };
        case 34: return { "34_tiltEQ", "頻譜傾斜", "shift=傾斜量,mul*", "類似Tilt EQ" };
        case 35: return { "35_hybrid", "pitch+fshift+mul", "shift,offsetBins,mul*", "複合實驗" };
        default: return { "?", "?", "?", "?" };
    }
}

bool isGlobalParam (const juce::String& id)
{
    return id == "wet" || id == "feedback" || id == "tilt" || id == "softClip"
        || id == "stereoSpread" || id == "phaseRandom" || id == "postGain"
        || id == "contrast" || id == "peakBoost" || id == "gateFloor"
        || id == "binSmooth" || id == "freeze";
}

// 依 DSP 實際用到的核心旋鈕鎖定；全局旋鈕永遠可動
bool coreParamActive (int exp, const juce::String& id)
{
    const bool shift = id == "shift";
    const bool shiftreel = id == "shiftreel";
    const bool shiftimage = id == "shiftimage";
    const bool mulreel = id == "mulreel";
    const bool mulimage = id == "mulimage";
    const bool offset = id == "offsetBins";

    switch (exp)
    {
        case 0:  return mulreel || mulimage;
        case 1:  return shift;
        case 2:  return shiftreel || shiftimage;
        case 3:  return offset;
        case 4:  return shift;
        case 5:  return shiftreel || mulreel;
        case 6:  return shift || shiftreel || mulreel;
        case 7:  return offset || mulreel || mulimage;
        case 8:  return mulreel || mulimage;
        case 9:  return offset || mulreel || mulimage;
        case 10: return offset || mulreel || mulimage;
        case 11: return offset || shift || mulreel || mulimage;
        case 12: return offset || shift || mulreel || mulimage;
        case 13: return offset || mulreel || mulimage;
        case 14: return mulreel || mulimage;
        case 15: return mulreel || mulimage;
        case 16: return offset || mulreel || mulimage;
        case 17: return shift || mulreel || mulimage;
        case 18: return offset || mulreel || mulimage;
        case 19: return shift || mulreel;
        case 20: return shiftimage || mulreel;
        case 21: return offset || mulreel || mulimage;
        case 22: return offset || mulreel || mulimage;
        case 23: return mulreel || mulimage;
        case 24: return mulreel || mulimage;
        case 25: return shiftreel || mulreel || mulimage;
        case 26: return shift || mulreel || mulimage;
        case 27: return mulreel || mulimage;
        case 28: return offset || shift || mulreel || mulimage;
        case 29: return offset || shift || mulreel || mulimage;
        case 30: return shift || shiftreel || mulreel || mulimage;
        case 31: return mulreel || mulimage;
        case 32: return shift || mulreel || mulimage;
        case 33: return mulreel || mulimage;
        case 34: return shift || mulreel || mulimage;
        case 35: return shift || offset || mulreel || mulimage;
        default: return false;
    }
}

} // namespace

NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), apvts (p.getAPVTS())
{
    titleLabel.setText (utf8 ("Spectral Lab IA — 亮起=此實驗有用；灰掉已鎖定"), juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (16.0f));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xfff2eee6));
    addAndMakeVisible (titleLabel);

    fftSizeLabel.setText ("fftSize", juce::dontSendNotification);
    fftSizeLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8e4dc));
    addAndMakeVisible (fftSizeLabel);
    fftSizeBox.addItemList (juce::StringArray { "512", "1024", "2048", "4096" }, 1);
    addAndMakeVisible (fftSizeBox);
    fftSizeAtt = std::make_unique<ComboAttachment> (apvts, "fftSize", fftSizeBox);

    experimentLabel.setText ("experiment", juce::dontSendNotification);
    experimentLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8e4dc));
    addAndMakeVisible (experimentLabel);
    experimentBox.addItemList (NewProjectAudioProcessor::experimentNames(), 1);
    addAndMakeVisible (experimentBox);
    experimentAtt = std::make_unique<ComboAttachment> (apvts, "experiment", experimentBox);

    const char* knobIds[] = {
        "shift", "shiftreel", "shiftimage", "mulreel", "mulimage", "offsetBins",
        "wet", "feedback", "tilt", "softClip", "stereoSpread", "phaseRandom",
        "postGain", "contrast", "peakBoost", "gateFloor", "binSmooth", "freeze"
    };

    knobPanel.setSize (320, (int) (sizeof (knobIds) / sizeof (knobIds[0])) * 28);
    for (auto* id : knobIds)
    {
        auto row = std::make_unique<KnobRow>();
        row->paramId = id;
        juce::String shown (id);
        if (shown == "wet") shown = "Dry/Wet";
        if (shown == "postGain") shown = "Output dB";
        row->label.setText (shown, juce::dontSendNotification);
        row->label.setColour (juce::Label::textColourId, juce::Colour (0xfff0c96a));
        knobPanel.addAndMakeVisible (row->label);

        if (juce::String (id) == "freeze")
        {
            row->isBool = true;
            knobPanel.addAndMakeVisible (row->toggle);
            row->batt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, row->toggle);
        }
        else
        {
            row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
            row->slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffc4a35a));
            knobPanel.addAndMakeVisible (row->slider);
            row->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, row->slider);
        }
        rows.push_back (std::move (row));
    }

    knobViewport.setViewedComponent (&knobPanel, false);
    addAndMakeVisible (knobViewport);

    helpLabel.setFont (juce::FontOptions (12.0f));
    helpLabel.setColour (juce::Label::textColourId, juce::Colour (0xffddd8ce));
    helpLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (helpLabel);

    apvts.addParameterListener ("experiment", this);
    apvts.addParameterListener ("fftSize", this);
    updateHelpText();
    setSize (900, 720);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    apvts.removeParameterListener ("experiment", this);
    apvts.removeParameterListener ("fftSize", this);
}

void NewProjectAudioProcessorEditor::parameterChanged (const juce::String& id, float)
{
    if (id == "experiment" || id == "fftSize")
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NewProjectAudioProcessorEditor> (this)]
        {
            if (safe != nullptr) safe->updateHelpText();
        });
}

void NewProjectAudioProcessorEditor::rebuildActiveHints()
{
    const int exp = (int) apvts.getRawParameterValue ("experiment")->load();
    for (auto& row : rows)
    {
        const bool active = isGlobalParam (row->paramId) || coreParamActive (exp, row->paramId);
        row->label.setAlpha (active ? 1.0f : 0.35f);
        row->label.setColour (juce::Label::textColourId,
                              active ? juce::Colour (0xfff0c96a) : juce::Colour (0xff7a7670));
        if (row->isBool)
        {
            row->toggle.setEnabled (active);
            row->toggle.setAlpha (active ? 1.0f : 0.35f);
        }
        else
        {
            row->slider.setEnabled (active);
            row->slider.setAlpha (active ? 1.0f : 0.35f);
        }
    }
}

void NewProjectAudioProcessorEditor::updateHelpText()
{
    rebuildActiveHints();

    const int exp = (int) apvts.getRawParameterValue ("experiment")->load();
    const int choice = (int) apvts.getRawParameterValue ("fftSize")->load();
    const int size = 512 << choice;
    const double hz = audioProcessor.getHzPerBin();
    const auto info = infoFor (exp);

    juce::String t;
    t << utf8 ("【當前實驗】") << utf8 (info.title) << "\n"
      << utf8 ("≈ 流行效果：") << utf8 (info.popular) << "\n"
      << utf8 ("此實驗主要旋鈕（已亮起）：") << utf8 (info.knobs) << "\n"
      << utf8 ("太大時：") << utf8 (info.danger) << "\n"
      << utf8 ("fftSize=") << size << utf8 ("，约 1 bin = ") << juce::String (hz, 3) << utf8 (" Hz\n")
      << utf8 ("防呆：灰色旋鈕已鎖定（轉了不變聲）。全局參數永遠可動。\n\n")
      << utf8 ("—— 全局參數（幾乎所有實驗都生效）——\n")
      << utf8 ("Dry/Wet：0=只乾聲（已延遲一個 FFT，避免梳狀），1=全濕。\n")
      << utf8 ("feedback：把輸出再送回輸入。大→嘯叫/無限延綿；>0.8很危險。\n")
      << utf8 ("tilt：頻譜傾斜。+抬高高頻，-抬低高頻。極端變很尖或很悶。\n")
      << utf8 ("softClip：軟削波。大→失真壓縮感；過大變方波糊。\n")
      << utf8 ("stereoSpread：左右用不同shift比例。大→很寬很分裂。\n")
      << utf8 ("phaseRandom：給相位加隨機。大→氣聲/噪聲感，定位模糊。\n")
      << utf8 ("Output：總音量，-24..+24 dB。0=不變；很大會爆音。\n")
      << utf8 ("contrast：拉大/壓平頻譜動態。>1更刺，<1更平。\n")
      << utf8 ("peakBoost：只加強超過平均值的峰。大→更尖更響。\n")
      << utf8 ("gateFloor：低於此振幅的 bin 清零。大→只剩強峰。\n")
      << utf8 ("binSmooth：鄰域平均半徑。大→糊。\n")
      << utf8 ("freeze：凍結當前頻譜再處理。開著會一直用同一幀音色。\n\n")
      << utf8 ("—— 核心 6 旋鈕（依實驗鎖定）——\n")
      << utf8 ("shift / shiftreel / shiftimage：bin讀取或寫入倍率（0.01~16）。\n")
      << utf8 ("  >>1 摺疊金屬；<<1 擠向低頻。\n")
      << utf8 ("mulreel / mulimage：振幅/相位倍率（0~16）。\n")
      << utf8 ("  mulreel大爆音；mulimage大相位碎。\n")
      << utf8 ("offsetBins：平移/截止/步進（±2048）。很大会循環或砍光。\n\n")
      << utf8 ("提示：先 Dry/Wet=1、feedback=0、Output=0 dB，只動亮起的旋鈕。");

    helpLabel.setText (t, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1b19));
    g.setColour (juce::Colour (0xff2a2824));
    g.fillRoundedRectangle (getLocalBounds().reduced (8).toFloat(), 8.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    titleLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);

    auto top = r.removeFromTop (26);
    fftSizeLabel.setBounds (top.removeFromLeft (70));
    fftSizeBox.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (12);
    experimentLabel.setBounds (top.removeFromLeft (80));
    experimentBox.setBounds (top);
    r.removeFromTop (8);

    auto left = r.removeFromLeft (340);
    knobViewport.setBounds (left);
    int y = 0;
    for (auto& row : rows)
    {
        row->label.setBounds (0, y, 110, 24);
        if (row->isBool)
            row->toggle.setBounds (120, y, 180, 24);
        else
            row->slider.setBounds (120, y, 200, 24);
        y += 28;
    }
    knobPanel.setSize (320, y);

    r.removeFromLeft (10);
    helpLabel.setBounds (r);
}
