cmake_minimum_required(VERSION 3.22)
project(AIFFT1 VERSION 1.1.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(AIFFT1
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.AIFFT1"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE AIFT
    FORMATS ${CHOUCHOU_PLUGIN_FORMATS}
    AAX_CATEGORY Effect
    PRODUCT_NAME "AIFFT1"
    DESCRIPTION "Spectral lab with dry/wet and output gain"
    VST3_CATEGORIES Fx)

juce_generate_juce_header(AIFFT1)

target_sources(AIFFT1
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(AIFFT1)

target_link_libraries(AIFFT1
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(AIFFTMixSelfTest PRODUCT_NAME "AIFFTMixSelfTest")
juce_generate_juce_header(AIFFTMixSelfTest)

target_sources(AIFFTMixSelfTest
    PRIVATE
        Tools/MixSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_include_directories(AIFFTMixSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(AIFFTMixSelfTest
    PRIVATE
        JucePlugin_Name="AIFFT1"
        CHOUCHOU_AIFFT_TEST=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(AIFFTMixSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
