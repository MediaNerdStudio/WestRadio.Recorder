cmake_minimum_required(VERSION 3.24)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(WestRadio.Recorder VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

# --- ASIO SDK (for PortAudio ASIO support) ---
include(FetchContent)
FetchContent_Declare(
    asiosdk
    GIT_REPOSITORY https://github.com/audiosdk/asio.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asiosdk)
set(ASIOSDK_ROOT_DIR ${asiosdk_SOURCE_DIR} CACHE PATH "ASIO SDK root" FORCE)

# --- PortAudio ---
set(PA_BUILD_SHARED OFF CACHE BOOL "Build PortAudio shared library" FORCE)
set(PA_BUILD_STATIC ON CACHE BOOL "Build PortAudio static library" FORCE)
set(PA_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_USE_ASIO ON CACHE BOOL "Enable PortAudio ASIO host API" FORCE)
FetchContent_Declare(
    portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG        v19.7.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(portaudio)

# The upstream target name changed a few times; make a safe alias.
if(NOT TARGET portaudio_static)
    message(FATAL_ERROR "PortAudio static target not found")
endif()

set(CORE_SOURCES
    src/core/AudioEngine.h
    src/core/AudioEngine.cpp
    src/core/DeviceStream.h
    src/core/DeviceStream.cpp
    src/core/AudioTrack.h
    src/core/AudioTrack.cpp
    src/core/AudioRingBuffer.h
    src/core/AudioRingBuffer.cpp
    src/core/WavStreamWriter.h
    src/core/WavStreamWriter.cpp
    src/core/FfmpegTask.h
    src/core/FfmpegTask.cpp
)

set(SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/ui/TrackWidget.h
    src/ui/TrackWidget.cpp
    src/ui/MeterWidget.h
    src/ui/MeterWidget.cpp
    ${CORE_SOURCES}
)

qt_add_executable(WestRadio.Recorder WIN32 ${SOURCES})

qt_add_executable(RecorderTest tests/test_record.cpp ${CORE_SOURCES})

target_include_directories(WestRadio.Recorder PRIVATE src)

target_link_libraries(WestRadio.Recorder PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    portaudio_static
)

if(WIN32)
    target_link_libraries(WestRadio.Recorder PRIVATE winmm)
endif()

set_target_properties(WestRadio.Recorder PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(RecorderTest PRIVATE src)
target_link_libraries(RecorderTest PRIVATE
    Qt6::Core
    portaudio_static
)
if(WIN32)
    target_link_libraries(RecorderTest PRIVATE winmm)
endif()
