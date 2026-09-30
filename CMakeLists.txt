cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(ChopShop VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

set(CHOPSHOP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(ChopShop
    COMPANY_NAME "MP Audio"
    COMPANY_WEBSITE "https://example.com"
    BUNDLE_ID com.mpaudio.chopshop
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Mpau
    PLUGIN_CODE Chp6
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ChopShop")

target_sources(ChopShop PRIVATE ${CHOPSHOP_SOURCES})

target_compile_definitions(ChopShop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChopShop
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless test harness: renders audio through every effect, checks the output,
# round-trips plugin state and saves a snapshot of the editor UI.
juce_add_console_app(ChopShopTest PRODUCT_NAME "ChopShopTest")
target_sources(ChopShopTest PRIVATE Tests/TestMain.cpp ${CHOPSHOP_SOURCES})
target_compile_definitions(ChopShopTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="ChopShop"
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ChopShopTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
