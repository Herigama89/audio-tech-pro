#!/usr/bin/env python3
"""Builds the web version's engine (WebAssembly) and copies the web app next to it.

    build.py <em++> <JUCE modules (patched: patch_juce.py)> <output folder> [--debug]

The engine: JUCE's modules the engine uses, Source/engine and Source/model (but plugin hosting), and Source/web.
"""
import os, sys, subprocess, pathlib, concurrent.futures, shutil

emxx, modules, out = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
debug = "--debug" in sys.argv
repo = pathlib.Path(__file__).resolve().parents[2]
objdir = out / "obj"
objdir.mkdir(parents=True, exist_ok=True)

defines = ["JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1", "JUCE_STANDALONE_APPLICATION=1", "JUCE_USE_CURL=0", "JUCE_WEB_BROWSER=0",
           "JUCE_PLUGINHOST_VST3=0", "JUCE_PLUGINHOST_AU=0", "JUCE_PLUGINHOST_LADSPA=0", "JUCE_PLUGINHOST_LV2=0",
           "JUCE_DISPLAY_SPLASH_SCREEN=0", "JUCE_USE_MP3AUDIOFORMAT=1", "NDEBUG=1"]
flags = ["-std=c++20", "-O3" if not debug else "-O1", "-fexceptions",
         f"-I{repo/'Tools/web/include'}", f"-I{modules}", f"-I{repo/'Source'}", f"-I{repo/'ThirdParty/onnxruntime/include'}"] + [f"-D{d}" for d in defines]

sources = [modules / m / f"{m}.cpp" for m in ["juce_core", "juce_events", "juce_data_structures", "juce_audio_basics",
                                             "juce_audio_formats", "juce_audio_devices", "juce_audio_processors_headless", "juce_audio_processors", "juce_dsp", "juce_graphics"]]
skip = {"ClapFormat.cpp", "PluginScanChild.cpp", "StemSeparator.cpp", "InterfaceControl.cpp", "PluginBuses.cpp"}
for folder in ["engine", "model"]:
    for f in sorted((repo / "Source" / folder).glob("*.cpp")):
        if f.name not in skip:
            sources.append(f)
sources.append(repo / "Source/web/WebHost.cpp")

def compile(src):
    obj = objdir / (src.parent.name + "_" + src.stem + ".o")
    if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime and src.parent.name.startswith("juce_"):
        return obj, ""
    r = subprocess.run([emxx] + flags + ["-c", str(src), "-o", str(obj)], capture_output=True, text=True)
    return (obj if r.returncode == 0 else None), r.stderr

objects, failed = [], []
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
    for src, (obj, err) in zip(sources, pool.map(compile, sources)):
        if obj is None:
            failed.append((src, err))
        else:
            objects.append(str(obj))
for src, err in failed:
    print(f"== {src}\n{err[-3000:]}")
if failed:
    sys.exit(1)

exports = ["_atp_effects", "_atp_add_effect", "_atp_remove_effect", "_atp_effect_params", "_atp_set_effect_param", "_atp_set_region", "_atp_move_region_to_track", "_atp_remove_track", "_atp_add_region", "_atp_add_track", "_atp_init", "_atp_open_zip", "_atp_describe", "_atp_new_project", "_atp_add_audio_track", "_atp_set_track",
           "_atp_set_track_name", "_atp_set_bypass", "_atp_set_master", "_atp_play", "_atp_stop", "_atp_seek", "_atp_position",
           "_atp_is_playing", "_atp_process", "_atp_levels", "_atp_waveform", "_atp_save_zip", "_atp_export_wav", "_malloc", "_free"]
link = [emxx] + objects + ["-o", str(out / "atp-engine.js"),
        "-O3" if not debug else "-O0", "-fexceptions",
        "-sMODULARIZE=1", "-sEXPORT_NAME=createAtpEngine", "-sENVIRONMENT=web,worker,shell,node",
        "-sALLOW_MEMORY_GROWTH=1", "-sINITIAL_MEMORY=64MB", "-sMAXIMUM_MEMORY=2GB", "-sSTACK_SIZE=1MB",
        "-sFILESYSTEM=1", "-sEXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPF32','HEAPU8']",
        "-sEXPORTED_FUNCTIONS=" + ",".join(exports),
        "--extern-post-js", str(repo / "Tools/web/expose.js"), "-Wl,--error-limit=0"]
if not debug:
    link += ["--closure=0", "-sASSERTIONS=0"]
r = subprocess.run(link, capture_output=True, text=True)
print(r.stdout[-3000:], r.stderr[-6000:])
if r.returncode != 0:
    sys.exit(1)

app = repo / "Web"
if app.exists():
    for f in app.iterdir():
        if f.is_file():
            shutil.copy2(f, out / f.name)
print("built", out)
