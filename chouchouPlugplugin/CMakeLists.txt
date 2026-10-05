# Same plugin as chouchouPlugplugin.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouPlugplugin VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouPlugplugin
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouPlugplugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cPlg
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouPlugplugin"
    DESCRIPTION "10-slot serial plugin rack"
    VST3_CATEGORIES Fx Tools)

juce_generate_juce_header(chouchouPlugplugin)

target_sources(chouchouPlugplugin
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginScanSupport.cpp
        Source/StandaloneApp.cpp)

chouchou_plugin_defaults(chouchouPlugplugin)

# Hosts VST3 (and AU on Mac) plugins inside its slots.
target_compile_definitions(chouchouPlugplugin
    PUBLIC
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST=0)

# On Windows the Standalone .exe doubles as the out-of-process plugin-scan worker
# (see PluginScanSupport.cpp). The Mac build keeps the default Standalone, as in the Xcode project.
if(WIN32)
    target_compile_definitions(chouchouPlugplugin PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_link_libraries(chouchouPlugplugin
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
