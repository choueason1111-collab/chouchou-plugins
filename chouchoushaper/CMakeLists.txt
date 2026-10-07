cmake_minimum_required(VERSION 3.22)
project(chouchouShaper VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

# AAX only when this folder is built on its own on this Mac (Pro Tools); the shared
# Windows / Mac build of all plugins ships VST3, AU and Standalone like the others.
set(chouchouShaper_FORMATS VST3 AU Standalone)
if(PROJECT_IS_TOP_LEVEL)
    list(APPEND chouchouShaper_FORMATS AAX)
endif()

juce_add_plugin(chouchouShaper
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouShaper"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cShp
    FORMATS ${chouchouShaper_FORMATS}
    AAX_CATEGORY Dynamics
    PRODUCT_NAME "chouchouShaper"
    DESCRIPTION "Dual envelope compressor: broadband time envelope + per-frequency envelope"
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(chouchouShaper)

target_sources(chouchouShaper
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ScopeView.cpp
)

chouchou_plugin_defaults(chouchouShaper)

target_link_libraries(chouchouShaper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Offline self-test: unity, contour compression, transient shaping, dry/wet, sidechain.
# Only when this folder is built on its own, not from the top-level build of all plugins.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

juce_add_console_app(ShaperSelfTest PRODUCT_NAME "ShaperSelfTest")
juce_generate_juce_header(ShaperSelfTest)

target_sources(ShaperSelfTest
    PRIVATE
        Tools/ShaperSelfTest/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ScopeView.cpp
)

target_include_directories(ShaperSelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(ShaperSelfTest
    PRIVATE
        CHOUCHOU_SHAPER_SELFTEST=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(ShaperSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
