cmake_minimum_required(VERSION 3.22)
project(FullertonVinyl VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to JUCE source")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

option(FV_COPY_PLUGIN "Copy the VST3 into the system VST3 folder after building" ON)

set(FV_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Skin.cpp
    Source/UI/Widgets.cpp)

juce_add_plugin(FullertonVinyl
    COMPANY_NAME "Fullerton Audio"
    BUNDLE_ID com.fullertonaudio.vinyl
    PLUGIN_MANUFACTURER_CODE Fula
    PLUGIN_CODE Fvnl
    FORMATS VST3 Standalone
    PRODUCT_NAME "Fullerton Vinyl"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${FV_COPY_PLUGIN}
    VST3_COPY_DIR "C:/Program Files/Common Files/VST3"
    VST3_CATEGORIES Fx Pitch\ Shift)

target_sources(FullertonVinyl PRIVATE ${FV_SOURCES})

target_compile_definitions(FullertonVinyl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FullertonVinyl
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Dev tool: runs the DSP self-tests and renders the editor to PNG files so the
# GUI can be checked without opening a host.   VinylDevTool <output-folder>
juce_add_console_app(VinylDevTool PRODUCT_NAME "VinylDevTool")
target_sources(VinylDevTool PRIVATE tools/DevTool.cpp ${FV_SOURCES})
target_compile_definitions(VinylDevTool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(VinylDevTool
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
