# CI/headless build for BlkFx. The .jucer remains the primary project file;
# this mirrors it closely enough for automated builds (JUCE fetched via CMake).
cmake_minimum_required(VERSION 3.22)

project(CognitoniBlkFx VERSION 0.2.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BlkFx
    PRODUCT_NAME "BlkFx"
    COMPANY_NAME "Cognitoni"
    COMPANY_WEBSITE "https://github.com/toni-lyttinen/CognitoniBlkFx"
    BUNDLE_ID com.cognitoni.blkfx
    FORMATS VST3 Standalone
    PLUGIN_MANUFACTURER_CODE Cogn
    PLUGIN_CODE Blkx
    VERSION ${PROJECT_VERSION})

target_sources(BlkFx PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Cards/AutoHarmCard.cpp
    Source/Cards/ContrastCard.cpp
    Source/Cards/SawsCard.cpp
    Source/Cards/SmearCard.cpp
    Source/SpectralEngine/FFTProcessor.cpp)

target_include_directories(BlkFx PRIVATE
    Source
    Source/Cards
    Source/SpectralEngine)

target_compile_definitions(BlkFx PUBLIC
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BlkFx PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_processors_headless
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)
