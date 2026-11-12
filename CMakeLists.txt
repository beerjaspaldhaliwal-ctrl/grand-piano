cmake_minimum_required(VERSION 3.22)

project(GrandPiano VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(GP_BUILD_TOOLS "Build the offline render test tool" OFF)

# ---------------------------------------------------------------------------
# JUCE (fetched automatically; pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# to use a local checkout instead)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(GP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GP_FORMATS AU)
endif()

juce_add_plugin(GrandPiano
    COMPANY_NAME                "GrandPianoAudio"
    COMPANY_WEBSITE             "https://github.com/beerjaspaldhaliwal-ctrl/grand-piano"
    BUNDLE_ID                   "com.grandpianoaudio.grandpiano"
    PLUGIN_MANUFACTURER_CODE    Gpno
    PLUGIN_CODE                 Gpn1
    FORMATS                     ${GP_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    PRODUCT_NAME                "Grand Piano"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Piano
    NEEDS_CURL                  TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(GrandPiano PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/engine/SfzParser.cpp
    Source/engine/SampleCache.cpp
    Source/engine/SampleManager.cpp
    Source/engine/SampleDownloader.cpp
    Source/engine/PianoEngine.cpp
    Source/ui/LookAndFeel.cpp)

target_include_directories(GrandPiano PRIVATE Source)

target_compile_definitions(GrandPiano PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(GrandPiano
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(GP_BUILD_TOOLS)
    juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest" NEEDS_CURL TRUE)
    target_sources(RenderTest PRIVATE tools/RenderTest.cpp)
    target_include_directories(RenderTest PRIVATE Source)
    target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0)
    target_link_libraries(RenderTest PRIVATE GrandPiano juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
