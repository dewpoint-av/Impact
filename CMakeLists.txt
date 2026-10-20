cmake_minimum_required(VERSION 3.21)
project(Impact VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DJUCE_VST3_CAN_REPLACE_VST2=0)
add_definitions(-DJUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

add_subdirectory(JUCE)
add_subdirectory(clap-juce-extensions)

set(IMPACT_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND IMPACT_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(Impact
    COMPANY_NAME "Dewpoint AV"
    COMPANY_WEBSITE "https://dewpoint.live"
    COMPANY_EMAIL "anthony@dewpoint.live"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Dwpt
    PLUGIN_CODE Impt
    FORMATS ${IMPACT_PLUGIN_FORMATS}
    PRODUCT_NAME "Impact"
    BUNDLE_ID "com.dewpoint.Impact"
)

clap_juce_extensions_plugin(TARGET Impact
    CLAP_ID "live.dewpoint.impact"
    CLAP_FEATURES audio-effect utility stereo)

juce_generate_juce_header(Impact)

target_sources(Impact
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/TransientVisualizer.cpp
        Source/dsp/TransientEngine.cpp
)

target_include_directories(Impact
    PRIVATE
        Source
)

target_compile_definitions(Impact
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Impact
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_data_structures
        juce::juce_events
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(Impact PRIVATE /W4)
else()
    target_compile_options(Impact PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    add_custom_command(TARGET Impact_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Impact_CLAP>"
            "${CMAKE_BINARY_DIR}/dist/Impact.clap"
        COMMENT "Staging Impact.clap")

    add_custom_command(TARGET Impact_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/Impact_artefacts/$<CONFIG>/VST3/Impact.vst3"
            "${CMAKE_BINARY_DIR}/dist/Impact.vst3"
        COMMENT "Staging Impact.vst3")
endif()
