cmake_minimum_required(VERSION 3.22)

project(bRiffer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime, so bRiffer.exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The generator, timing and MIDI-file code: plain C++, no JUCE, so the tests build fast.
add_library(briffer_core STATIC
    src/core/Engine.cpp
    src/core/MidiFileWriter.cpp)
target_include_directories(briffer_core PUBLIC src/core)

juce_add_plugin(bRiffer
    COMPANY_NAME "bFunc"
    COMPANY_CODE Bfnc
    PLUGIN_MANUFACTURER_CODE Bfnc
    PLUGIN_CODE Brif
    PRODUCT_NAME "bRiffer"
    # v0.1 is the standalone app only; VST3/AU come in v0.2.
    FORMATS Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    # The app opens its own MIDI output port (picker in the top bar).
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(bRiffer PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/GridComponent.cpp
    src/ui/LaneComponent.cpp)

target_include_directories(bRiffer PRIVATE src)

target_compile_definitions(bRiffer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(bRiffer
    PRIVATE
        briffer_core
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_executable(briffer_tests tests/EngineTests.cpp)
target_link_libraries(briffer_tests PRIVATE briffer_core)
add_test(NAME engine COMMAND briffer_tests)
