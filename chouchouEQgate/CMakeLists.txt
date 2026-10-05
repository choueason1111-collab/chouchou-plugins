# Same plugin as NewProject.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouEQGate2 VERSION 1.0.1)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouEQGate2
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouEQGate2"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cEQ2
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouEQGate2"
    DESCRIPTION "Spectral expander"
    VST3_CATEGORIES Fx Dynamics)

juce_generate_juce_header(chouchouEQGate2)

target_sources(chouchouEQGate2
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouEQGate2)

target_link_libraries(chouchouEQGate2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
