cmake_minimum_required(VERSION 3.22)

project(Riff VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# JUCE 8 — fetched automatically on first configure (takes a minute or two).
# To use a local checkout instead:  cmake -B build -DRIFF_JUCE_DIR=/path/to/JUCE
# ---------------------------------------------------------------------------
set(RIFF_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")

if(RIFF_JUCE_DIR)
    add_subdirectory(${RIFF_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(Riff
    PRODUCT_NAME "Riff"
    COMPANY_NAME "Riff"
    BUNDLE_ID    "com.riff.daw"
    # Needed on macOS so the OS lets us use the microphone / audio interface inputs
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Riff needs audio input access to record."
)

juce_generate_juce_header(Riff)

target_sources(Riff PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/engine/AudioEngine.cpp
)

target_include_directories(Riff PRIVATE Source)

target_compile_definitions(Riff PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Riff,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Riff,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Riff
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
