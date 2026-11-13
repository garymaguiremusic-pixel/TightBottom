cmake_minimum_required(VERSION 3.22)

project(TightBottom VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(TIGHTBOTTOM_BUILD_STANDALONE "Also build a standalone app for quick testing" OFF)
option(TIGHTBOTTOM_BUILD_TESTS      "Build the DSP unit tests"                      ON)
option(TIGHTBOTTOM_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# JUCE (pinned release tag)
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin
set(TIGHTBOTTOM_FORMATS VST3)
if(TIGHTBOTTOM_BUILD_STANDALONE)
    list(APPEND TIGHTBOTTOM_FORMATS Standalone)
endif()

if(CMAKE_CROSSCOMPILING)
    set(TIGHTBOTTOM_VST3_MANIFEST FALSE)
else()
    set(TIGHTBOTTOM_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(TightBottom
    PRODUCT_NAME                "TIGHT BOTTOM"
    COMPANY_NAME                "TightBottom Project"
    BUNDLE_ID                   com.tightbottomproject.tightbottom
    PLUGIN_MANUFACTURER_CODE    Tbpj
    PLUGIN_CODE                 Tbt1
    FORMATS                     ${TIGHTBOTTOM_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    VST3_AUTO_MANIFEST          ${TIGHTBOTTOM_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     ${TIGHTBOTTOM_COPY_AFTER_BUILD})

target_sources(TightBottom PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/Components.cpp)

target_include_directories(TightBottom PRIVATE Source)

target_compile_definitions(TightBottom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TightBottom
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Unit tests (plain C++, the DSP does not depend on JUCE)
if(TIGHTBOTTOM_BUILD_TESTS)
    enable_testing()
    add_executable(TightBottomTests Tests/Tests.cpp)
    target_include_directories(TightBottomTests PRIVATE Source)
    add_test(NAME TightBottomTests COMMAND TightBottomTests)
endif()
