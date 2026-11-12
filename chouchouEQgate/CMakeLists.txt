# Spectral gate — builds both chouchouEQGate and chouchouEQGate2 from the same sources.
cmake_minimum_required(VERSION 3.22)
project(chouchouEQGate VERSION 1.0.1)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

# ---- Gate 1 (legacy AU subtype Qmvl / manufacturer Manu) ----
juce_add_plugin(chouchouEQGate
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouEQGate"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Qmvl
    FORMATS ${CHOUCHOU_PLUGIN_FORMATS}
    AAX_CATEGORY Dynamics
    PRODUCT_NAME "chouchouEQGate"
    DESCRIPTION "Spectral gate"
    VST3_CATEGORIES Fx Dynamics)

juce_generate_juce_header(chouchouEQGate)

target_sources(chouchouEQGate
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouEQGate)

target_link_libraries(chouchouEQGate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Gate 2 (current branded product) ----
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
    FORMATS ${CHOUCHOU_PLUGIN_FORMATS}
    AAX_CATEGORY Dynamics
    PRODUCT_NAME "chouchouEQGate2"
    DESCRIPTION "Spectral gate"
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

# ---- Self-test (links Gate2 product macros) ----
juce_add_console_app(GateSelfTest PRODUCT_NAME "GateSelfTest")
juce_generate_juce_header(GateSelfTest)
target_sources(GateSelfTest
    PRIVATE
        Tools/GateSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
target_include_directories(GateSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(GateSelfTest
    PRIVATE
        JucePlugin_Name="chouchouEQGate2"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(GateSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
