cmake_minimum_required(VERSION 3.22)

project(Riff VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RIFF_BUILD_TESTS "Build the unit tests" ON)

# ---------------------------------------------------------------------------
# Dependencies, fetched on first configure.
# To use a local JUCE checkout instead:  cmake -B build -DRIFF_JUCE_DIR=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)

set(RIFF_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")

if(RIFF_JUCE_DIR)
    add_subdirectory(${RIFF_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Sources shared by the app and the tests (model + engine; no UI)
# ---------------------------------------------------------------------------
set(RIFF_CORE_SOURCES
    Source/model/Project.cpp
    Source/engine/AudioEngine.cpp
    Source/engine/AudioFileCache.cpp
    Source/engine/ProjectPlayer.cpp
    Source/engine/LatencyMeasure.cpp
    Source/engine/BuiltInSynth.cpp
    Source/engine/PluginInstrument.cpp
    Source/engine/InstrumentRack.cpp
)

set(RIFF_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
)

# Audio Units exist only on macOS.
if(APPLE)
    list(APPEND RIFF_DEFINITIONS JUCE_PLUGINHOST_AU=1)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(Riff
    PRODUCT_NAME "Riff"
    COMPANY_NAME "Riff"
    BUNDLE_ID    "com.riff.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Riff needs audio input access to record."
)

juce_generate_juce_header(Riff)

target_sources(Riff PRIVATE
    ${RIFF_CORE_SOURCES}
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ui/Theme.cpp
    Source/ui/TrackHeaders.cpp
    Source/ui/ArrangeCanvas.cpp
    Source/ui/LatencyCalibrator.cpp
    Source/ui/PianoRoll.cpp
    Source/ui/InstrumentWindows.cpp
)

target_include_directories(Riff PRIVATE Source)

target_compile_definitions(Riff PRIVATE
    ${RIFF_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Riff,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Riff,JUCE_VERSION>"
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

# ---------------------------------------------------------------------------
# Tests:  cmake --build build --target RiffTests && ./build/RiffTests_artefacts/Debug/RiffTests
# ---------------------------------------------------------------------------
if(RIFF_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(RiffTests PRODUCT_NAME "RiffTests")
    juce_generate_juce_header(RiffTests)

    target_sources(RiffTests PRIVATE
        ${RIFF_CORE_SOURCES}
        Tests/TestMain.cpp
        Tests/ModelTests.cpp
        Tests/EngineTests.cpp
    )

    target_include_directories(RiffTests PRIVATE Source)
    target_compile_definitions(RiffTests PRIVATE ${RIFF_DEFINITIONS})

    # Tests compare exact silence (0.0f) on purpose.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(Tests/ModelTests.cpp Tests/EngineTests.cpp
            PROPERTIES COMPILE_OPTIONS -Wno-float-equal)
    endif()

    target_link_libraries(RiffTests
        PRIVATE
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_graphics
            juce::juce_audio_processors
            Catch2::Catch2
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
