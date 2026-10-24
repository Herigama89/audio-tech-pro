#pragma once
// The JUCE modules the web version of the engine uses (WebAssembly: no plugin formats, no devices of its own)
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#if JUCE_TARGET_HAS_BINARY_DATA
 #include "BinaryData.h"
#endif
#if ! DONT_SET_USING_JUCE_NAMESPACE
 using namespace juce;
#endif
