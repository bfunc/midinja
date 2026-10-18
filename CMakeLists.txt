cmake_minimum_required(VERSION 3.22)

project(Midinja VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime, so Midinja.exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The generator, timing and MIDI-file code: plain C++, no JUCE, so the tests build fast.
add_library(midinja_core STATIC
    src/core/Engine.cpp
    src/core/Generate.cpp
    src/core/Form.cpp
    src/core/MidiFileWriter.cpp
    src/Document.cpp)
target_include_directories(midinja_core PUBLIC src/core src)
set_target_properties(midinja_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Midinja
    COMPANY_NAME "bFunc"
    COMPANY_CODE Bfnc
    PLUGIN_MANUFACTURER_CODE Bfnc
    PLUGIN_CODE Mdnj
    PRODUCT_NAME "Midinja"
    BUNDLE_ID cc.bfunc.midinja
    FORMATS Standalone VST3
    VST3_CATEGORIES Instrument Synth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    # The notes also go out as MIDI: to the host (VST3) and to the app's own MIDI port.
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(Midinja PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/GridComponent.cpp
    src/ui/LaneComponent.cpp
    src/ui/PartsBar.cpp
    src/ui/TabBar.cpp
    src/ui/VersionsStrip.cpp
    src/ui/Widgets.cpp)

target_include_directories(Midinja PRIVATE src)

target_compile_definitions(Midinja PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(Midinja
    PRIVATE
        midinja_core
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_executable(midinja_tests tests/EngineTests.cpp)
target_link_libraries(midinja_tests PRIVATE midinja_core)
add_test(NAME engine COMMAND midinja_tests)
