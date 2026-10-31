cmake_minimum_required(VERSION 3.22)
project(chouchouFFTEQ VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouFFTEQ
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouFFTEQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE CFEQ
    FORMATS VST3 AU AAX Standalone
    AAX_CATEGORY EQ
    PRODUCT_NAME "chouchouFFTEQ"
    DESCRIPTION "Graphic EQ and combinable FFT with sidechain spectrum"
    VST3_CATEGORIES Fx EQ)

juce_generate_juce_header(chouchouFFTEQ)

target_sources(chouchouFFTEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouFFTEQ)

target_link_libraries(chouchouFFTEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(FFTEQSelfTest PRODUCT_NAME "FFTEQSelfTest")
juce_generate_juce_header(FFTEQSelfTest)

target_sources(FFTEQSelfTest
    PRIVATE
        Tools/FFTEQSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_include_directories(FFTEQSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(FFTEQSelfTest
    PRIVATE
        JucePlugin_Name="chouchouFFTEQ"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(FFTEQSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
