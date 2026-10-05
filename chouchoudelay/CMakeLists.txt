# Same plugin as chouchouDelay.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouDelay VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouDelay
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouDelay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cDly
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouDelay"
    DESCRIPTION "Multi-tap delay with per-echo level, time offset and pitch shift"
    VST3_CATEGORIES Fx Delay)

juce_generate_juce_header(chouchouDelay)

target_sources(chouchouDelay
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouDelay)

target_link_libraries(chouchouDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
