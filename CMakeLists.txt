cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) by default on macOS.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(HeronEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HERON_COPY_AFTER_BUILD "Install the built plugins into the user plug-in folders" OFF)

# ---------------------------------------------------------------------------
# JUCE: fetched automatically. To use a local checkout instead, pass
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(HERON_FORMATS VST3 AU AAX Standalone)
else()
    set(HERON_FORMATS VST3 Standalone)
endif()

juce_add_plugin(HeronEQ
    COMPANY_NAME                "Lindell Plugins"
    BUNDLE_ID                   "com.lindellplugins.ibis"
    PLUGIN_MANUFACTURER_CODE    Lndl
    PLUGIN_CODE                 Ibis
    PRODUCT_NAME                "Lindell - Ibis"
    FORMATS                     ${HERON_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${HERON_COPY_AFTER_BUILD}
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                EQ)

# GUI bitmaps (rendered by tools/render_assets.py)
juce_add_binary_data(HeronAssets HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES
    assets/panel.png
    assets/window.png
    assets/knob_large_strip.png
    assets/knob_small_strip.png
    assets/button_red_strip.png
    assets/button_black_strip.png
    assets/button_white_strip.png
    assets/toggle_strip.png
    assets/lamp_strip.png)

target_sources(HeronEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(HeronEQ PRIVATE Source)

target_compile_definitions(HeronEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HeronEQ
    PRIVATE
        HeronAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# DSP unit tests (no JUCE): ctest --test-dir build
# ---------------------------------------------------------------------------
enable_testing()
add_executable(HeronDSPTests Tests/DSPTests.cpp)
target_include_directories(HeronDSPTests PRIVATE Source)
add_test(NAME HeronDSPTests COMMAND HeronDSPTests)

# ---------------------------------------------------------------------------
# Optional host smoke test: loads the built VST3 like a DAW would.
#   cmake -DHERON_BUILD_HOST_TEST=ON ...
#   HeronHostTest "build/HeronEQ_artefacts/Release/VST3/Heron EQ.vst3"
# ---------------------------------------------------------------------------
option(HERON_BUILD_HOST_TEST "Build the VST3 host smoke test" OFF)
if(HERON_BUILD_HOST_TEST)
    juce_add_console_app(HeronHostTest PRODUCT_NAME "HeronHostTest")
    target_sources(HeronHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(HeronHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(HeronHostTest PRIVATE
        juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
