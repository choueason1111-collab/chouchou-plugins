# Same plugin as chouchouTrigger.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouTrigger VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouTrigger
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouTrigger"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cTrg
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouTrigger"
    DESCRIPTION "Threshold drum trigger: fires up to six layered samples with pitch shift"
    VST3_CATEGORIES Fx Generator)

juce_generate_juce_header(chouchouTrigger)

target_sources(chouchouTrigger
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouTrigger)

target_link_libraries(chouchouTrigger
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
