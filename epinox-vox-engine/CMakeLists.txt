cmake_minimum_required(VERSION 3.22)

project(EPINOX_VOX_ENGINE VERSION 0.1.0 LANGUAGES C CXX)

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
