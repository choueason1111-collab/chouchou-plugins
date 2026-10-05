# Shared setup for the chouchou plugins. Each plugin's CMakeLists.txt includes this file, so a
# plugin builds on its own (cmake -S <plugin folder>) or together with the others from the
# top-level CMakeLists.txt.
#
# Paths default to this Mac's layout and can be overridden, e.g. on a build server:
#   -DCHOUCHOU_JUCE_DIR=/path/to/JUCE -DCHOUCHOU_ARA_SDK_DIR=/path/to/ARA_SDK

include_guard(GLOBAL)

set(CHOUCHOU_JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "JUCE source folder")
set(CHOUCHOU_ARA_SDK_DIR "$ENV{HOME}/SDKs/ARA_SDK" CACHE PATH "Celemony ARA SDK folder")
option(CHOUCHOU_COPY_AFTER_BUILD "Install each plugin into the system plugin folder after building" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT COMMAND juce_add_plugin)
    add_subdirectory("${CHOUCHOU_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
endif()

# Settings every plugin target shares, matching the Projucer projects.
function(chouchou_plugin_defaults target)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
            JUCE_DISPLAY_SPLASH_SCREEN=0)
    # The sources contain UTF-8 French text; MSVC otherwise reads them in the local code page.
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()
