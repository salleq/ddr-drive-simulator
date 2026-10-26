cmake_minimum_required(VERSION 3.22)

project(DDR VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on macOS (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Use a local JUCE checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Audio Unit as well on macOS, so Logic and GarageBand users can run DDR too
if(APPLE)
    set(DDR_FORMATS VST3 AU Standalone)
else()
    set(DDR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(DDR
    COMPANY_NAME "salleq"
    COMPANY_WEBSITE "https://github.com/salleq/ddr-drive-simulator"
    BUNDLE_ID "com.salleq.ddr"
    PLUGIN_MANUFACTURER_CODE Sllq
    PLUGIN_CODE Ddr1
    PRODUCT_NAME "DDR Drive Simulator"
    DESCRIPTION "Dreaded Drive Simulator - hear your mix in a stock family car"
    FORMATS ${DDR_FORMATS}
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DDR PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/dsp/MusicChain.cpp
    Source/dsp/NoiseGenerator.cpp)

target_include_directories(DDR PRIVATE Source)

target_compile_definitions(DDR PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DDR
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional developer tool: renders the editor to PNG files (used for screenshots in the README)
option(DDR_BUILD_TOOLS "Build the screenshot tool" OFF)
if(DDR_BUILD_TOOLS)
    juce_add_console_app(ddr_snapshot PRODUCT_NAME "ddr_snapshot")
    target_sources(ddr_snapshot PRIVATE
        tools/Snapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/dsp/MusicChain.cpp
        Source/dsp/NoiseGenerator.cpp)
    target_include_directories(ddr_snapshot PRIVATE Source)
    target_compile_definitions(ddr_snapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="DDR")
    target_link_libraries(ddr_snapshot PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    juce_add_console_app(ddr_analyze PRODUCT_NAME "ddr_analyze")
    target_sources(ddr_analyze PRIVATE tools/Analyze.cpp Source/Parameters.cpp Source/dsp/MusicChain.cpp Source/dsp/NoiseGenerator.cpp)
    target_include_directories(ddr_analyze PRIVATE Source)
    target_compile_definitions(ddr_analyze PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ddr_analyze PRIVATE
        juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
