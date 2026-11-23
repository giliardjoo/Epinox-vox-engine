cmake_minimum_required(VERSION 3.22)

# Link the Microsoft runtime statically so the plugin loads on any Windows PC, without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(EPINOX_VOX_ENGINE VERSION 0.17.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(EPINOX_FORMATS VST3 AU Standalone)
else()
    set(EPINOX_FORMATS VST3 Standalone)
endif()

juce_add_plugin(EpinoxVoxEngine
    COMPANY_NAME                "EPINOX"
    PLUGIN_MANUFACTURER_CODE    Epnx
    PLUGIN_CODE                 Vxen
    FORMATS                     ${EPINOX_FORMATS}
    PRODUCT_NAME                "EPINOX VOX ENGINE"
    BUNDLE_ID                   "com.epinox.voxengine"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(EpinoxVoxEngine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/GenerationService.cpp
    Source/UI/NeonLookAndFeel.cpp)

target_include_directories(EpinoxVoxEngine PRIVATE Source)

# The official EPINOX logo is embedded when the file is present.
set(EPINOX_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Logo/epinox_logo.png")
if(EXISTS "${EPINOX_LOGO}")
    juce_add_binary_data(EpinoxAssets SOURCES "${EPINOX_LOGO}")
    target_link_libraries(EpinoxVoxEngine PRIVATE EpinoxAssets)
    target_compile_definitions(EpinoxVoxEngine PRIVATE EPINOX_HAS_LOGO=1)
else()
    target_compile_definitions(EpinoxVoxEngine PRIVATE EPINOX_HAS_LOGO=0)
endif()

target_compile_definitions(EpinoxVoxEngine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(EpinoxVoxEngine
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Numerical tests of the audio engines:  cmake -B build -DEPINOX_BUILD_TESTS=ON && ctest --test-dir build -C Release
option(EPINOX_BUILD_TESTS "Build the DSP test executable" OFF)

if(EPINOX_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(EpinoxDspTests PRODUCT_NAME "EpinoxDspTests")
    target_sources(EpinoxDspTests PRIVATE Tests/DspTests.cpp)
    target_include_directories(EpinoxDspTests PRIVATE Source)
    target_compile_definitions(EpinoxDspTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(EpinoxDspTests PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME dsp COMMAND EpinoxDspTests)

    juce_add_console_app(EpinoxServiceTest PRODUCT_NAME "EpinoxServiceTest")
    target_sources(EpinoxServiceTest PRIVATE Tests/GenerationServiceTest.cpp Source/GenerationService.cpp)
    target_include_directories(EpinoxServiceTest PRIVATE Source)
    target_compile_definitions(EpinoxServiceTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EpinoxServiceTest PRIVATE juce::juce_events juce::juce_recommended_config_flags)
    add_test(NAME generation_service COMMAND EpinoxServiceTest)
endif()
