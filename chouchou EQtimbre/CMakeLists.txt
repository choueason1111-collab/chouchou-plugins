cmake_minimum_required(VERSION 3.22)
project(chouchouEQtimbre VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouEQtimbre
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouEQtimbre"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cTmb
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouEQtimbre"
    DESCRIPTION "Even / odd harmonic rendering"
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
)

juce_generate_juce_header(chouchouEQtimbre)

target_sources(chouchouEQtimbre
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

chouchou_plugin_defaults(chouchouEQtimbre)

target_link_libraries(chouchouEQtimbre
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Offline self-test: measures harmonic levels produced by the processor.
# Only when this folder is built on its own, not from the top-level build of all plugins.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

juce_add_console_app(TimbreSelfTest PRODUCT_NAME "TimbreSelfTest")
juce_generate_juce_header(TimbreSelfTest)

target_sources(TimbreSelfTest
    PRIVATE
        Tools/TimbreSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(TimbreSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(TimbreSelfTest
    PRIVATE
        CHOUCHOU_TIMBRE_SELFTEST=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(TimbreSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
