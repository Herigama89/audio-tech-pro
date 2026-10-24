#!/usr/bin/env python3
"""Copies JUCE's modules and patches the few places JUCE 8 doesn't yet handle WebAssembly (JUCE_WASM).

    patch_juce.py <juce modules folder> <output folder>
"""
import shutil, sys, pathlib

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(src, dst)

def patch(rel, old, new):
    p = dst / rel
    text = p.read_text()
    if old not in text:
        sys.exit(f"patch_juce: '{old[:40]}...' not found in {rel}")
    p.write_text(text.replace(old, new, 1))

# WebAssembly is little-endian and 32-bit (wasm32); JUCE 8 leaves both unsaid
patch("juce_core/system/juce_TargetPlatform.h", "#elif defined (__wasm__)\n  #define       JUCE_WASM 1",
      "#elif defined (__wasm__)\n  #define       JUCE_WASM 1\n  #define       JUCE_LITTLE_ENDIAN 1\n  #define       JUCE_32BIT 1")

# emscripten_get_now and friends
patch("juce_core/juce_core.cpp", "#elif JUCE_WASM\n #include \"native/juce_SystemStats_wasm.cpp\"",
      "#elif JUCE_WASM\n #include \"native/juce_CommonFile_linux.cpp\"\n #include \"native/juce_SystemStats_wasm.cpp\"")
patch("juce_core/juce_core.cpp", " #if JUCE_WASM\n  #include <stdio.h>",
      " #if JUCE_WASM\n  #include <emscripten.h>\n  #include <dirent.h>\n  #include <fnmatch.h>\n  #ifndef FNM_CASEFOLD\n   #define FNM_CASEFOLD 0x10\n  #endif\n  #include <stdio.h>")
# directory listing needs the stat helper that JUCE leaves out of its WebAssembly build
patch("juce_core/native/juce_SharedCode_posix.h",
      "   #if JUCE_MAC || JUCE_IOS\n    static int64 getCreationTime",
      "   #endif\n\n   #if JUCE_MAC || JUCE_IOS\n    static int64 getCreationTime")
patch("juce_core/native/juce_SharedCode_posix.h",
      "            *isReadOnly = access (path.toUTF8(), W_OK) != 0;\n    }\n   #endif\n",
      "            *isReadOnly = access (path.toUTF8(), W_OK) != 0;\n    }\n")
# thread priorities: as Linux's (the browser has none)
patch("juce_core/native/juce_ThreadPriorities_native.h", "       #if JUCE_LINUX || JUCE_BSD\n        { Thread::Priority::highest,    0 },",
      "       #if JUCE_LINUX || JUCE_BSD || JUCE_WASM\n        { Thread::Priority::highest,    0 },")
# settings files: where Linux keeps them (the browser's file system)
patch("juce_data_structures/app_properties/juce_PropertiesFile.cpp", "   #elif JUCE_LINUX || JUCE_BSD || JUCE_ANDROID\n    auto dir",
      "   #elif JUCE_LINUX || JUCE_BSD || JUCE_ANDROID || JUCE_WASM\n    auto dir")
# which host: none
patch("juce_audio_processors/utilities/juce_PluginHostType.cpp", "   #elif JUCE_IOS\n   #elif JUCE_ANDROID\n   #else\n    #error",
      "   #elif JUCE_IOS\n   #elif JUCE_ANDROID\n   #elif JUCE_WASM\n   #else\n    #error")
print("patched JUCE for WebAssembly in", dst)

# MIDI 2.0 endpoints: none in the browser (Web MIDI is the page's own business)
p = dst / "juce_audio_devices/juce_audio_devices.cpp"
p.write_text(p.read_text() + """
#if JUCE_WASM
namespace juce::universal_midi_packets
{
auto Endpoints::Impl::Native::make (EndpointsListener&) -> std::unique_ptr<Native> { return nullptr; }
}
#endif
""")
