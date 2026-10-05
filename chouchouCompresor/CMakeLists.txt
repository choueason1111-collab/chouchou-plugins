# Same plugin as chouchouCompressor.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouCompressor VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouCompressor
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouCompressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cCp1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouCompressor"
    DESCRIPTION "Dual-threshold spectral compressor"
    VST3_CATEGORIES Fx Dynamics)

juce_generate_juce_header(chouchouCompressor)

target_sources(chouchouCompressor
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouCompressor)

target_link_libraries(chouchouCompressor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
