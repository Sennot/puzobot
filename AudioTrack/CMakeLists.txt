cmake_minimum_required(VERSION 3.21)

project(XdBotAudiotrack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set")
endif()

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/xdbot_api.cpp
    src/menu_inject.cpp
    src/track.cpp
    src/wav.cpp
    src/playback.cpp
    src/editor_popup.cpp
    src/mixer.cpp
    src/installer.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
# xdBot is closed source and not on the Geode index, so it is an external
# dependency (still required at runtime through mod.json).
setup_geode_mod(${PROJECT_NAME} EXTERNALS "zilko.xdbot:2.7.7")

# WASAPI (mixer), SetupAPI-free: the installer only starts PowerShell through ShellExecute.
target_link_libraries(${PROJECT_NAME} ole32 shell32 propsys)
