cmake_minimum_required(VERSION 3.22)
project(OnnoPlugins VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

# JUCE: lokaler Pfad (-DJUCE_DIR=...) oder automatisch von GitHub laden
set(JUCE_DIR "" CACHE PATH "Pfad zu einem JUCE-Checkout (leer = herunterladen)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(ONNO_TESTS "Test-Programm bauen (Rendering und Messungen)" OFF)

# --- Onno VC-1 ---
juce_add_plugin(OnnoVC1
    COMPANY_NAME "Onno"
    PLUGIN_MANUFACTURER_CODE Onno
    PLUGIN_CODE Ovc1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Onno VC-1"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(OnnoVC1 PRIVATE
    Source/VC1/VC1Processor.cpp
    Source/VC1/VC1Editor.cpp)
target_include_directories(OnnoVC1 PRIVATE Source)
target_compile_definitions(OnnoVC1 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(OnnoVC1
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(ONNO_TESTS)
    juce_add_console_app(OnnoTests PRODUCT_NAME "OnnoTests")
    target_sources(OnnoTests PRIVATE
        Tests/Tests.cpp
        Source/VC1/VC1Processor.cpp
        Source/VC1/VC1Editor.cpp)
    target_include_directories(OnnoTests PRIVATE Source)
    target_compile_definitions(OnnoTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ONNO_TEST_BUILD=1)
    target_link_libraries(OnnoTests PRIVATE juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
