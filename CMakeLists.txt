cmake_minimum_required(VERSION 3.22)

# macOS: runs on macOS 11 and later, on Apple Silicon and Intel Macs.
# A universal (both) build:  cmake -B build-universal -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# (or scripts/build-mac-universal.sh). Must be set before project().
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the app runs on")
endif()

project(AudioTechPro VERSION 1.1.2 LANGUAGES C CXX)

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

# ARA (Melodyne, VocAlign, RX... working right on the regions, as in Logic and Pro Tools): the ARA SDK
# (Apache 2.0), fetched with its parts. Turn off with -DRIFF_ARA=OFF.
option(RIFF_ARA "Host ARA plugins" ON)
if(RIFF_ARA)
    FetchContent_Declare(ara_sdk
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG        releases/2.2.0
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES_RECURSE TRUE
        SOURCE_SUBDIR  do-not-build)   # (headers and library sources only; nothing of it is built on its own)
    FetchContent_MakeAvailable(ara_sdk)
    juce_set_ara_sdk_path("${ara_sdk_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Sources shared by the app and the tests (model + engine; no UI)
# ---------------------------------------------------------------------------
# CLAP plugin hosting: the CLAP SDK headers (MIT), vendored.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/clap/include)

# Opening Pro Tools sessions: libptformat (LGPL 2.1, from the Ardour project), vendored.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ptformat)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime/include)   # (headers only: the runtime is loaded when stems are first split)
set_source_files_properties(ThirdParty/ptformat/ptformat.cc PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w;/UDEBUG,-w;-UDEBUG>")

set(RIFF_CORE_SOURCES
    Source/engine/StockEq.cpp
    Source/engine/StockDynamics.cpp
    Source/engine/StockDelay.cpp
    Source/engine/StockReverb.cpp
    Source/engine/StockModulation.cpp
    Source/engine/StockDistortion.cpp
    Source/engine/StockFilter.cpp
    Source/engine/StockAmps.cpp
    Source/engine/StockPitch.cpp
    Source/engine/StockUtility.cpp
    Source/engine/StockExtras.cpp
    Source/engine/StockPedalboard.cpp
    Source/engine/StockMetering.cpp
    Source/engine/StockMulti.cpp
    Source/engine/StockRoomSim.cpp
    Source/engine/StockLegacy.cpp
    Source/engine/PluginScanChild.cpp
    Source/import/ProjectImport.cpp
    Source/import/ReadAbleton.cpp
    Source/import/ReadDawProject.cpp
    Source/import/ReadFlStudio.cpp
    Source/import/ReadStudioOne.cpp
    Source/import/ReadProTools.cpp
    Source/import/ReadLogicCubase.cpp
    Source/import/ReadMidiFile.cpp
    ThirdParty/ptformat/ptformat.cc
    Source/engine/ClapFormat.cpp
    Source/engine/RegionVariants.cpp
    Source/engine/AudioSuite.cpp
    Source/engine/AraHost.cpp
    Source/model/Project.cpp
    Source/model/Presets.cpp
    Source/model/Automix.cpp
    Source/model/NoteTools.cpp
    Source/engine/PitchCorrect.cpp
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
    Source/engine/AudioEdit.cpp
    Source/engine/LatencyMeasure.cpp
    Source/engine/BuiltInSynth.cpp
    Source/engine/BuiltInInstruments.cpp
    Source/engine/PluginInstrument.cpp
    Source/engine/InstrumentRack.cpp
    Source/engine/BuiltInEffects.cpp
    Source/engine/EffectRack.cpp
    Source/engine/MasterBusCompressor.cpp
    Source/engine/ConsoleChannel.cpp
    Source/engine/TapeSaturation.cpp
    Source/engine/PreampSaturation.cpp
    Source/engine/RenderThreads.cpp
    Source/engine/InterfaceControl.cpp
    Source/engine/StemSeparator.cpp
    Source/engine/KeyDetect.cpp
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

# Windows (Visual Studio): big source files, UTF-8 sources, parallel compiles.
if(MSVC)
    add_compile_options(/bigobj /utf-8 /MP)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

# Audio Units exist only on macOS.
if(APPLE)
    list(APPEND RIFF_DEFINITIONS JUCE_PLUGINHOST_AU=1)
endif()

if(RIFF_ARA)
    list(APPEND RIFF_DEFINITIONS JUCE_PLUGINHOST_ARA=1 RIFF_HAS_ARA=1)
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
                README.md
                # The mixer skin, cut from Resources/skin/source.jpg by Tools/make_skin_from_image.py
                Resources/skin/strip_top.png
                Resources/skin/strip_face.png
                Resources/skin/slot.png
                Resources/skin/send.png
                Resources/skin/db_box.png
                Resources/skin/fader_cap.png
                Resources/skin/knob_black.png
                Resources/skin/knob_white.png
                Resources/skin/knob_bus.png
                Resources/skin/vu_face.png
                Resources/skin/panel.png
                # The Studio White skin, cut from your pictures by Tools/make_white_skin.py
                # Themes > Faders / Knobs / Buttons / Bus Comp meter (Tools/make_look_pieces.py)
                Resources/skin/looks/look_cap_black.png
                Resources/skin/looks/look_cap_black_red.png
                Resources/skin/looks/look_cap_silver.png
                Resources/skin/looks/look_knob_black.png
                Resources/skin/looks/look_knob_blackyellow.png
                Resources/skin/looks/look_knob_yellow.png
                Resources/skin/looks/look_knob_red.png
                Resources/skin/looks/look_btn_square_white.png
                Resources/skin/looks/look_btn_square_grey.png
                Resources/skin/looks/look_btn_round.png
                Resources/skin/looks/look_vu_black.png
                Resources/skin/looks/look_vu_cream.png
                Resources/skin/looks/look_vu_amber.png
                Resources/skin/looks/look_cap_dark.png
                Resources/skin/looks/look_knob_dark.png
                Resources/skin/looks/look_btn_dark.png
                Resources/skin/white/white_fader_cap.png
                Resources/skin/white/white_fader_cap_black.png
                Resources/skin/white/white_fader_rail.png
                Resources/skin/white/white_knob_body.png
                Resources/skin/white/white_knob_ring.png
                Resources/skin/white/white_led_on.png
                Resources/skin/white/white_led_clip.png
                # Xenon, cut from your picture by Tools/make_xenon_skin.py
                Resources/skin/xenon/xenon_knob.png
                Resources/skin/xenon/xenon_cap.png
                Resources/skin/xenon/xenon_slot.png
                Resources/skin/xenon/xenon_meter.png
                Resources/skin/xenon/xenon_panel.png
                Resources/skin/xenon/xenon_bar.png)

# Everything on screen except the app's entry point (the snapshot tool reuses it).
set(RIFF_UI_SOURCES
    Source/MainComponent.cpp
    Source/MainCommands.cpp
    Source/MainAssistant.cpp
    Source/MainExtras.cpp
    Source/MainStems.cpp
    Source/MainKey.cpp
    Source/MainScreenRecording.cpp
    Source/ui/PluginHostPanel.cpp
    Source/ui/AudioSuitePanel.cpp
    Source/ui/MultiMeter.cpp
    Source/ui/StartupSplash.cpp
    Source/MainAudioDevices.cpp
    Source/ui/Theme.cpp
    Source/ui/ThemePack.cpp
    Source/ui/Looks.cpp
    Source/ui/InsertBar.cpp
    Source/ui/AutomixPanel.cpp
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
    Source/ui/EffectFace.cpp
    Source/ui/EffectDesigns.cpp
    Source/ui/Mixer.cpp
    Source/ui/ConsoleStripWindow.cpp
    Source/ui/EventList.cpp
    Source/ui/LiveLoopsGrid.cpp
    Source/ui/PluginPicker.cpp
    Source/ui/ThemePanel.cpp
    Source/ui/SpectrumAnalyzer.cpp
    Source/ui/LibraryPanel.cpp
    Source/ui/AudioEditorPanel.cpp
    Source/ui/PitchEditor.cpp
    Source/ui/SmartControls.cpp
    Source/ui/NoteViews.cpp
    Source/ui/PlayerEditor.cpp
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
        Tests/AudioEditTests.cpp
        Tests/ClapTests.cpp
        Tests/ImportTests.cpp
        Tests/ThemeTests.cpp
        Tests/AutomixTests.cpp
        Tests/NoteToolsTests.cpp
        Tests/StockEffectsTests.cpp
        Source/ui/Theme.cpp
        Source/ui/ThemePack.cpp
        Source/ui/ScreenRecordPanel.cpp
        Source/ui/ScreenRecorder.cpp
        Source/ui/SpectrumAnalyzer.cpp
    )

    target_include_directories(AudioTechProTests PRIVATE Source)
    target_compile_definitions(AudioTechProTests PRIVATE ${RIFF_DEFINITIONS})

    # Two tiny CLAP plugins the tests load.
    add_library(TestClapPlugins MODULE Tests/clap/TestClapPlugins.cpp)
    set_target_properties(TestClapPlugins PROPERTIES PREFIX "" SUFFIX ".clap" CXX_VISIBILITY_PRESET hidden)
    add_dependencies(AudioTechProTests TestClapPlugins)
    target_compile_definitions(AudioTechProTests PRIVATE TEST_CLAP_PATH="$<TARGET_FILE:TestClapPlugins>")
    target_compile_definitions(AudioTechProTests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/data")

    # Tests compare exact silence (0.0f) on purpose.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(Tests/ClapTests.cpp Tests/ModelTests.cpp Tests/EngineTests.cpp Tests/GeneratorTests.cpp Tests/AssistantTests.cpp Tests/ScreenRecordingTests.cpp Tests/EditTests.cpp
            PROPERTIES COMPILE_OPTIONS -Wno-float-equal)
    endif()

    target_link_libraries(AudioTechProTests
        PRIVATE
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_graphics
            juce::juce_audio_processors
            juce::juce_dsp
            Catch2::Catch2
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()

# Hosting Audio Units needs CoreAudioKit (for the generic plugin view).
if(APPLE)
    # Screen recording (ScreenCaptureKit is weak-linked: the rest of the app runs on older macOS)
    target_sources(AudioTechPro PRIVATE Source/ui/ScreenRecorder_mac.mm Source/ui/PluginWindowKeys_mac.mm)
    set_source_files_properties(Source/ui/PluginWindowKeys_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

    # Sign the app so macOS's privacy permissions (screen recording, microphone) stay with it when it's
    # rebuilt: the requirement names the app's identifier, not this build's fingerprint.
    add_custom_command(TARGET AudioTechPro POST_BUILD
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/Tools/sign_app.sh" "$<TARGET_BUNDLE_DIR:AudioTechPro>"
        COMMENT "Signing Audio Tech Pro (keeps its permissions across updates)"
        VERBATIM)
    set_source_files_properties(Source/ui/ScreenRecorder_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(AudioTechPro PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
                                               "-weak_framework ScreenCaptureKit")
    target_link_libraries(AudioTechPro PRIVATE "-framework CoreAudioKit")
    # The window drawn by the graphics card (View > Draw with the Graphics Card)
    target_link_libraries(AudioTechPro PRIVATE juce::juce_opengl)
    target_compile_definitions(AudioTechPro PRIVATE RIFF_GPU_DRAWING=1 GL_SILENCE_DEPRECATION=1)
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
    target_compile_definitions(Snapshots PRIVATE ${RIFF_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1 RIFF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(Snapshots PRIVATE AudioTechProData juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp)
    if(APPLE)
        target_sources(Snapshots PRIVATE Source/ui/ScreenRecorder_mac.mm Source/ui/PluginWindowKeys_mac.mm)
        target_link_libraries(Snapshots PRIVATE "-framework CoreAudioKit" "-framework AVFoundation" "-framework CoreMedia"
                                                "-framework CoreVideo" "-weak_framework ScreenCaptureKit")
    endif()
endif()

# The stem separator on its own, for checking it against Demucs itself (CI: .github/workflows/stem-models.yml):
#   StemCheck <onnxruntime library> <model.onnx> <in.wav> <out folder>
option(RIFF_BUILD_STEMCHECK "Build the stem separation check tool" OFF)

if(RIFF_BUILD_STEMCHECK)
    juce_add_console_app(StemCheck PRODUCT_NAME "StemCheck")
    juce_generate_juce_header(StemCheck)
    target_sources(StemCheck PRIVATE Tools/StemCheck.cpp Source/engine/StemSeparator.cpp Source/engine/Resampler.cpp)
    target_include_directories(StemCheck PRIVATE Source)
    target_compile_definitions(StemCheck PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(StemCheck PRIVATE juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
endif()
