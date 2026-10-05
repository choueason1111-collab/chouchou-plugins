cmake_minimum_required(VERSION 3.22)
project(chouchouPlugplugin2 VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/chouchou.cmake")

juce_set_ara_sdk_path("${CHOUCHOU_ARA_SDK_DIR}")

juce_add_plugin(chouchouPlugplugin2
    COMPANY_NAME "chouchou"
    BUNDLE_ID "com.chouchou.chouchouPlugplugin2"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${CHOUCHOU_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE ChoU
    PLUGIN_CODE cPg2
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "chouchouPlugplugin2"
    DESCRIPTION "10-slot serial rack with ARA host"
    VST3_CATEGORIES Fx Tools
)

juce_generate_juce_header(chouchouPlugplugin2)

target_sources(chouchouPlugplugin2
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginScanSupport.cpp
        Source/StandaloneApp.cpp
        Source/ARAHostSupport.cpp
        Source/Audition/StandaloneLauncher.cpp
        Source/Audition/HostProbe.cpp
)

target_include_directories(chouchouPlugplugin2
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_compile_definitions(chouchouPlugplugin2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_ARA=1
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        CHOUCHOU_ENABLE_EMBEDDED_ARA_EXPERIMENT=0
)

target_link_libraries(chouchouPlugplugin2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Soften warnings from third-party ARA / nested host headers
if(APPLE)
    target_compile_options(chouchouPlugplugin2 PRIVATE -Wno-deprecated-declarations)
endif()
if(MSVC)
    target_compile_options(chouchouPlugplugin2 PRIVATE /utf-8)
endif()

# The self-tests below need SpectraLayers on this Mac; skip them in the top-level build of all plugins.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

#------------------------------------------------------------------------------
# Offline ARA smoke test (SpectraLayers + embedded host) — no Audition
#------------------------------------------------------------------------------
juce_add_console_app(chouchouARASelfTest
    PRODUCT_NAME "chouchouARASelfTest"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

juce_generate_juce_header(chouchouARASelfTest)

target_sources(chouchouARASelfTest
    PRIVATE
        Tools/ARASelfTest/main.cpp
        Source/ARAHostSupport.cpp
)

target_include_directories(chouchouARASelfTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_compile_definitions(chouchouARASelfTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_ARA=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(chouchouARASelfTest
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(APPLE)
    target_compile_options(chouchouARASelfTest PRIVATE -Wno-deprecated-declarations)
endif()

#------------------------------------------------------------------------------
# Offline rack self-test, linked against the plug-in's current shared code
#------------------------------------------------------------------------------
add_executable(chouchouRackSelfTest EXCLUDE_FROM_ALL Tools/RackSelfTest/main.cpp)

target_include_directories(chouchouRackSelfTest
    PRIVATE
        $<TARGET_PROPERTY:chouchouPlugplugin2,INCLUDE_DIRECTORIES>
)

target_compile_definitions(chouchouRackSelfTest
    PRIVATE
        $<TARGET_PROPERTY:chouchouPlugplugin2,COMPILE_DEFINITIONS>
)

target_link_libraries(chouchouRackSelfTest PRIVATE chouchouPlugplugin2)

if(APPLE)
    target_compile_options(chouchouRackSelfTest PRIVATE -Wno-deprecated-declarations)
endif()
