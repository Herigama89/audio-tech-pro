cmake_minimum_required(VERSION 3.22)

project(AudioTechPro VERSION 0.8.0 LANGUAGES C CXX)

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
    Source/model/Generator.cpp
    Source/model/ProjectFiles.cpp
    Source/engine/AggregateDevice.cpp
    Source/ai/ClaudeApi.cpp
    Source/ai/Assistant.cpp
    Source/ai/AssistantTools.cpp
    Source/engine/AudioEngine.cpp
    Source/engine/AudioFileCache.cpp
    Source/engine/ProjectPlayer.cpp
    Source/engine/AutomationWriter.cpp
    Source/engine/LatencyMeasure.cpp
    Source/engine/BuiltInSynth.cpp
    Source/engine/PluginInstrument.cpp
    Source/engine/InstrumentRack.cpp
    Source/engine/BuiltInEffects.cpp
    Source/engine/EffectRack.cpp
    Source/engine/MasterBusCompressor.cpp
    Source/engine/ConsoleChannel.cpp
    Source/engine/ConsolePlugins.cpp
    Source/engine/Resampler.cpp
)

set(RIFF_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
)

# Converting sample rates while playing is heavy maths: optimise it even in Debug
# builds, so a 44.1 kHz song plays smoothly in a 48 kHz session.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(Source/engine/Resampler.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()

# Audio Units exist only on macOS.
if(APPLE)
    list(APPEND RIFF_DEFINITIONS JUCE_PLUGINHOST_AU=1)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(AudioTechPro
    PRODUCT_NAME "Audio Tech Pro"
    COMPANY_NAME "Audio Tech Pro"
    BUNDLE_ID    "com.audiotechpro.app"
    ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio Tech Pro needs audio input access to record."
)

juce_generate_juce_header(AudioTechPro)

# Artwork compiled into the app
juce_add_binary_data(AudioTechProData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     Resources/Logo.png
                README.md)

# Everything on screen except the app's entry point (the snapshot tool reuses it).
set(RIFF_UI_SOURCES
    Source/MainComponent.cpp
    Source/MainCommands.cpp
    Source/MainAssistant.cpp
    Source/MainExtras.cpp
    Source/MainScreenRecording.cpp
    Source/MainAudioDevices.cpp
    Source/ui/Theme.cpp
    Source/ui/ConsoleLook.cpp
    Source/ui/Commands.cpp
    Source/ui/KeyCommands.cpp
    Source/ui/GeneratorPanel.cpp
    Source/ui/AssistantPanel.cpp
    Source/ui/ScreenRecorder.cpp
    Source/ui/ScreenRecordPanel.cpp
    Source/ui/CombineInterfacesPanel.cpp
    Source/ui/Inspector.cpp
    Source/ui/TrackHeaders.cpp
    Source/ui/ArrangeCanvas.cpp
    Source/ui/LatencyCalibrator.cpp
    Source/ui/PianoRoll.cpp
    Source/ui/InstrumentWindows.cpp
    Source/ui/Mixer.cpp
    Source/ui/ConsoleStripWindow.cpp
    Source/ui/EventList.cpp
    Source/ui/BusCompConsolePanel.cpp
    Source/ui/StripParts.cpp
)

target_sources(AudioTechPro PRIVATE
    ${RIFF_CORE_SOURCES}
    ${RIFF_UI_SOURCES}
    Source/Main.cpp
)

target_include_directories(AudioTechPro PRIVATE Source)

target_compile_definitions(AudioTechPro PRIVATE
    ${RIFF_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AudioTechPro,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AudioTechPro,JUCE_VERSION>"
)

target_link_libraries(AudioTechPro
    PRIVATE
        AudioTechProData
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
# Tests:  cmake --build build --target AudioTechProTests && ./build/AudioTechProTests_artefacts/Debug/AudioTechProTests
# ---------------------------------------------------------------------------
if(RIFF_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(AudioTechProTests PRODUCT_NAME "AudioTechProTests")
    juce_generate_juce_header(AudioTechProTests)

    target_sources(AudioTechProTests PRIVATE
        ${RIFF_CORE_SOURCES}
        Source/ui/Commands.cpp
        Tests/TestMain.cpp
        Tests/ModelTests.cpp
        Tests/EngineTests.cpp
        Tests/CommandTests.cpp
        Tests/ConsoleTests.cpp
        Tests/GeneratorTests.cpp
        Tests/AssistantTests.cpp
        Tests/ScreenRecordingTests.cpp
        Tests/DeviceTests.cpp
        Tests/ViewTests.cpp
        Tests/EditTests.cpp
        Tests/FilesTests.cpp
        Tests/AutomationWriterTests.cpp
        Source/ui/ScreenRecordPanel.cpp
        Source/ui/ScreenRecorder.cpp
    )

    target_include_directories(AudioTechProTests PRIVATE Source)
    target_compile_definitions(AudioTechProTests PRIVATE ${RIFF_DEFINITIONS})

    # Tests compare exact silence (0.0f) on purpose.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(Tests/ModelTests.cpp Tests/EngineTests.cpp Tests/GeneratorTests.cpp Tests/AssistantTests.cpp Tests/ScreenRecordingTests.cpp Tests/EditTests.cpp
            PROPERTIES COMPILE_OPTIONS -Wno-float-equal)
    endif()

    target_link_libraries(AudioTechProTests
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

# Hosting Audio Units needs CoreAudioKit (for the generic plugin view).
if(APPLE)
    # Screen recording (ScreenCaptureKit is weak-linked: the rest of the app runs on older macOS)
    target_sources(AudioTechPro PRIVATE Source/ui/ScreenRecorder_mac.mm)
    set_source_files_properties(Source/ui/ScreenRecorder_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(AudioTechPro PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
                                               "-weak_framework ScreenCaptureKit")
    target_link_libraries(AudioTechPro PRIVATE "-framework CoreAudioKit")
    if(TARGET AudioTechProTests)
        target_link_libraries(AudioTechProTests PRIVATE "-framework CoreAudioKit")
    endif()
endif()

# ---------------------------------------------------------------------------
# UI snapshots: renders the interface to PNG files without a screen (for
# checking the look on build machines). Not part of the app.
# ---------------------------------------------------------------------------
option(RIFF_BUILD_SNAPSHOTS "Build the UI snapshot tool" OFF)

if(RIFF_BUILD_SNAPSHOTS)
    juce_add_console_app(Snapshots PRODUCT_NAME "Snapshots")
    juce_generate_juce_header(Snapshots)
    target_sources(Snapshots PRIVATE Tools/Snapshots.cpp ${RIFF_CORE_SOURCES} ${RIFF_UI_SOURCES})
    target_include_directories(Snapshots PRIVATE Source)
    target_compile_definitions(Snapshots PRIVATE ${RIFF_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(Snapshots PRIVATE AudioTechProData juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp)
    if(APPLE)
        target_sources(Snapshots PRIVATE Source/ui/ScreenRecorder_mac.mm)
        target_link_libraries(Snapshots PRIVATE "-framework CoreAudioKit" "-framework AVFoundation" "-framework CoreMedia"
                                                "-framework CoreVideo" "-weak_framework ScreenCaptureKit")
    endif()
endif()
