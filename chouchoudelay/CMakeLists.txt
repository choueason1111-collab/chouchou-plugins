# Same plugin as chouchouDelay.jucer (which still works for Xcode builds).
cmake_minimum_required(VERSION 3.22)
project(chouchouDelay VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_add_plugin(chouchouDelay
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouDelay"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cDly
    FORMATS ${CHOUCHOU_PLUGIN_FORMATS}
    AAX_CATEGORY Delay
    PRODUCT_NAME "chouchouDelay"
    DESCRIPTION "Multi-tap delay with pan, tap tempo, real-time tempo and note rhythms"
    VST3_CATEGORIES Fx Delay)

juce_generate_juce_header(chouchouDelay)

target_sources(chouchouDelay
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

chouchou_plugin_defaults(chouchouDelay)

target_link_libraries(chouchouDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(DelaySelfTest PRODUCT_NAME "DelaySelfTest")
juce_generate_juce_header(DelaySelfTest)
target_sources(DelaySelfTest PRIVATE Tools/DelaySelfTest/main.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(DelaySelfTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(DelaySelfTest PRIVATE
    JucePlugin_Name="chouchouDelay" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(DelaySelfTest PRIVATE
    juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(DelayEditorSnapshot PRODUCT_NAME "DelayEditorSnapshot")
juce_generate_juce_header(DelayEditorSnapshot)
target_sources(DelayEditorSnapshot PRIVATE Tools/EditorSnapshot/main.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(DelayEditorSnapshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(DelayEditorSnapshot PRIVATE
    JucePlugin_Name="chouchouDelay" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(DelayEditorSnapshot PRIVATE
    juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
