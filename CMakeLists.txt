# Builds all chouchou plugins at once (used by the GitHub Actions build for Windows and Mac).
# Each plugin folder still builds on its own, and the .jucer / Xcode projects are unchanged.
cmake_minimum_required(VERSION 3.22)
project(chouchouPlugins VERSION 1.0.0)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/chouchou.cmake")

add_subdirectory("chouchou EQtimbre")
add_subdirectory("chouchouCompresor")
add_subdirectory("chouchouEQgate")
add_subdirectory("chouchoudelay")
add_subdirectory("chouchoutrigger")
add_subdirectory("chouchouPlugplugin")
add_subdirectory("chouchouPlugplugin 2")
