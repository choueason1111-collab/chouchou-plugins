cmake_minimum_required(VERSION 3.22)
project(chouchouFFT1 VERSION 1.1.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouFFT1
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouFFT1"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE CFF1
    FORMATS ${CHOUCHOU_PLUGIN_FORMATS}
    AAX_CATEGORY PitchShift
    PRODUCT_NAME "chouchouFFT1"
    DESCRIPTION "Spectral bin experiments with dry/wet and output gain"
    VST3_CATEGORIES Fx)

juce_generate_juce_header(chouchouFFT1)

target_sources(chouchouFFT1
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouFFT1)

target_link_libraries(chouchouFFT1
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(FFTMixSelfTest PRODUCT_NAME "FFTMixSelfTest")
juce_generate_juce_header(FFTMixSelfTest)

target_sources(FFTMixSelfTest
    PRIVATE
        Tools/MixSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_include_directories(FFTMixSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(FFTMixSelfTest
    PRIVATE
        JucePlugin_Name="chouchouFFT1"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(FFTMixSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
