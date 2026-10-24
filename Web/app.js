// Audio Tech Pro Web: the page. The engine (the desktop app's own, built for WebAssembly) runs on the browser's
// audio thread in engine-worklet.js; this file is the arrange window, the mixer, the transport and the files.
"use strict";

const $ = id => document.getElementById(id);
const READABLE = ["wav", "aif", "aiff", "flac", "ogg"];   // read by the engine as they are
const PALETTE = ["#5c8ee6", "#e0607e", "#e8a23a", "#6cc070", "#b07ce0", "#4fc1c9", "#d9d35a", "#e07a4f"];
const TRACK_H = () => parseInt(getComputedStyle(document.documentElement).getPropertyValue("--track-h")) || 84;

let ctx = null, node = null, ready = false;
let project = { name: "Untitled", tempo: 120, length: 0, masterGain: 0.8, tracks: [] };
let selectedTrack = null, selectedRegion = null;
let position = 0, playing = false, levels = new Float32Array(0);
let pxPerSec = 40;
let dirty = false;
const waveforms = new Map();          // region id -> Float32Array of min/max pairs (100 a second)
const meterState = new Map();         // canvas -> { l, r, holdL, holdR, holdT }
let wasmBytes = fetch("atp-engine.wasm").then(r => { if (! r.ok) throw new Error("atp-engine.wasm: " + r.status); return r.arrayBuffer(); });

// ---------------------------------------------------------------- engine messaging
let nextId = 1;
const pending = new Map();
function rpc(msg, transfer = []) {
    return new Promise((resolve, reject) => {
        const id = nextId++;
        pending.set(id, { resolve, reject });
        node.port.postMessage({ id, ...msg }, transfer);
    });
}
const call = (fn, ...args) => rpc({ op: "call", fn, args });
const callJ = (fn, ...args) => rpc({ op: "call", fn, args, text: true });

let recording = null;   // { trackId, start, chunks: [[L, R]], stream, source }

function onEngineMessage(e) {
    const m = e.data;
    if (m.id) {
        const p = pending.get(m.id);
        pending.delete(m.id);
        if (p) m.error ? p.reject(new Error(m.error)) : p.resolve(m.result);
        return;
    }
    if (m.type === "tick") {
        position = m.position;
        if (playing !== m.playing) { playing = m.playing; $("playBtn").classList.toggle("on", playing); $("playBtn").textContent = playing ? "■" : "▶"; }
        levels = m.levels;
        if (playing) followPlayhead();
    } else if (m.type === "input" && recording) {
        recording.chunks.push([m.left, m.right]);
    } else if (m.type === "log") {
        console.log("[engine]", m.text);
    }
}

let starting = null;
function startEngine() { return starting || (starting = boot().catch(e => { starting = null; ctx = null; alertError("Couldn't start the engine: " + e.message); throw e; })); }
async function boot() {
    status("Starting the engine…");
    ctx = new AudioContext({ latencyHint: "interactive" });
    await ctx.audioWorklet.addModule("worklet-shim.js");
    await ctx.audioWorklet.addModule("atp-engine.js");
    await ctx.audioWorklet.addModule("engine-worklet.js");
    const wasm = await wasmBytes;
    node = new AudioWorkletNode(ctx, "atp-engine", {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: "explicit",
        processorOptions: { wasm, rate: ctx.sampleRate },
    });
    await new Promise((resolve, reject) => {
        node.port.onmessage = e => {
            if (e.data.type === "ready") resolve();
            else if (e.data.type === "failed") reject(new Error(e.data.text));
        };
    });
    node.port.onmessage = onEngineMessage;
    node.connect(ctx.destination);
    ready = true;
    status("Ready · " + Math.round(ctx.sampleRate) + " Hz");
    requestAnimationFrame(frame);
}

// ---------------------------------------------------------------- project
function setProject(desc, keepWaveforms = false) {
    if (! desc || desc.error) { if (desc && desc.error) alertError(desc.error); return; }
    project = desc;
    if (! keepWaveforms) waveforms.clear();
    for (const t of project.tracks) for (const r of t.regions) if (! r.midi && ! r.missing && ! waveforms.has(r.id)) loadWaveform(r);
    if (selectedTrack && ! project.tracks.some(t => t.id === selectedTrack)) selectedTrack = null;
    if (selectedRegion && ! findRegion(selectedRegion)) selectedRegion = null;
    $("tempoView").textContent = Math.round(project.tempo * 100) / 100;
    document.title = (project.name || "Untitled") + " · Audio Tech Pro Web";
    buildHeaders();
    buildMixer();
    layout();
}
async function refresh() { setProject(await callJ("atp_describe"), true); }
function changed() { dirty = true; }

async function loadWaveform(region) {
    const points = Math.max(16, Math.min(60000, Math.ceil(region.length * 100)));
    try {
        const data = await rpc({ op: "waveform", region: region.id, points });
        if (data && data.length) { waveforms.set(region.id, data); drawTimeline(); }
    } catch (e) {}
}

function findRegion(id) {
    for (const t of project.tracks) for (const r of t.regions) if (r.id === id) return { track: t, region: r };
    return null;
}
function colourOf(t, i) {
    const c = (t.colour || "").replace(/^#/, "");
    if (/^[0-9a-f]{8}$/i.test(c)) return "#" + c.slice(2);
    if (/^[0-9a-f]{6}$/i.test(c)) return "#" + c;
    return PALETTE[i % PALETTE.length];
}
const toDb = g => g <= 0.001 ? -60 : Math.max(-60, Math.min(6, 20 * Math.log10(g)));
const fromDb = db => db <= -60 ? 0 : Math.pow(10, db / 20);
const fmtDb = db => db <= -60 ? "-∞" : (db > 0 ? "+" : "") + db.toFixed(1);

// ---------------------------------------------------------------- files
async function uniquePath(name) {
    const safe = name.replace(/[\/\\:*?"<>|]/g, "_"), dot = safe.lastIndexOf(".");
    const base = dot > 0 ? safe.slice(0, dot) : safe, ext = dot > 0 ? safe.slice(dot) : "";
    let path = "/project/Audio Files/" + safe, n = 2;
    while (usedPaths.has(path) || await rpc({ op: "exists", path })) path = "/project/Audio Files/" + base + " " + n++ + ext;
    usedPaths.add(path);
    return path;
}
const usedPaths = new Set();

async function importFiles(files) {
    if (! ready) await startEngine();
    for (const file of files) {
        const ext = file.name.split(".").pop().toLowerCase();
        if (ext === "zip") { await openZip(file); continue; }
        status("Importing " + file.name + "…");
        try {
            let bytes = await file.arrayBuffer(), name = file.name;
            if (! READABLE.includes(ext)) {
                const audio = await ctx.decodeAudioData(bytes.slice(0));
                bytes = encodeWav([audio.getChannelData(0), audio.numberOfChannels > 1 ? audio.getChannelData(1) : audio.getChannelData(0)], audio.sampleRate);
                name = file.name.replace(/\.[^.]+$/, "") + ".wav";
            }
            const path = await uniquePath(name);
            await rpc({ op: "write", path, data: bytes }, [bytes]);
            const desc = await callJ("atp_add_audio_track", path, position);
            if (desc.error) { alertError(desc.error); continue; }
            setProject(desc, true);
            changed();
        } catch (e) { alertError("Couldn't import " + file.name + ": " + e.message); }
    }
    status("Ready");
}

async function openZip(file) {
    if (! ready) await startEngine();
    if (dirty && ! confirm("Open " + file.name + "? Changes to this project that aren't saved will be lost.")) return;
    status("Opening " + file.name + "…");
    const data = await file.arrayBuffer();
    try {
        const desc = await rpc({ op: "openZip", data }, [data]);
        if (desc.error) { alertError(desc.error); status("Ready"); return; }
        usedPaths.clear();
        selectedRegion = selectedTrack = null;
        setProject(desc);
        dirty = false;
        hideOverlay();
        const missing = desc.tracks.reduce((n, t) => n + t.regions.filter(r => r.missing).length, 0);
        status(missing ? "Opened · " + missing + " region" + (missing === 1 ? "" : "s") + " missing audio (not in the zip)" : "Opened " + (desc.name || file.name));
    } catch (e) { alertError(e.message); }
}

async function newProject() {
    if (! ready) await startEngine();
    if (dirty && ! confirm("Start a new project? Changes to this one that aren't saved will be lost.")) return;
    usedPaths.clear();
    selectedRegion = selectedTrack = null;
    setProject(await callJ("atp_new_project", "Untitled"));
    dirty = false;
    hideOverlay();
    status("New project");
}

async function saveZip() {
    if (! ready) return;
    status("Saving…");
    const r = await callJ("atp_save_zip", "/out.zip");
    if (r.error) { alertError(r.error); return; }
    const bytes = await rpc({ op: "read", path: "/out.zip" });
    download(new Blob([bytes], { type: "application/zip" }), (project.name || "Untitled") + ".zip");
    dirty = false;
    status("Saved " + (project.name || "Untitled") + ".zip · opens in the desktop app with File › Open");
    remember(bytes);
}

async function exportWav() {
    if (! ready) return;
    if (project.length <= 0) { alertError("There's nothing to export yet."); return; }
    status("Exporting the mix…");
    await call("atp_stop");
    const r = await callJ("atp_export_wav", "/mix.wav", 0, project.length + 2);
    if (r.error) { alertError(r.error); return; }
    const bytes = await rpc({ op: "read", path: "/mix.wav" });
    download(new Blob([bytes], { type: "audio/wav" }), (project.name || "Untitled") + ".wav");
    status("Exported " + (project.name || "Untitled") + ".wav (24-bit)");
}

function download(blob, name) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
}

function encodeWav(channels, rate) {
    const frames = channels[0].length, nch = channels.length, buf = new ArrayBuffer(44 + frames * nch * 4), v = new DataView(buf);
    const w = (o, s) => { for (let i = 0; i < s.length; ++i) v.setUint8(o + i, s.charCodeAt(i)); };
    w(0, "RIFF"); v.setUint32(4, 36 + frames * nch * 4, true); w(8, "WAVE"); w(12, "fmt ");
    v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, nch, true); v.setUint32(24, rate, true);
    v.setUint32(28, rate * nch * 4, true); v.setUint16(32, nch * 4, true); v.setUint16(34, 32, true);
    w(36, "data"); v.setUint32(40, frames * nch * 4, true);
    let o = 44;
    for (let i = 0; i < frames; ++i) for (let c = 0; c < nch; ++c, o += 4) v.setFloat32(o, channels[c][i], true);
    return buf;
}

// The last saved project, kept in this browser so a closed tab can be picked up again.
function db() {
    return new Promise((resolve, reject) => {
        const r = indexedDB.open("atp-web", 1);
        r.onupgradeneeded = () => r.result.createObjectStore("projects");
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => reject(r.error);
    });
}
async function remember(bytes) {
    try {
        const d = await db();
        d.transaction("projects", "readwrite").objectStore("projects").put({ name: project.name, when: Date.now(), bytes: bytes.slice(0) }, "last");
    } catch (e) {}
}
async function recalled() {
    try {
        const d = await db();
        return await new Promise(resolve => {
            const r = d.transaction("projects").objectStore("projects").get("last");
            r.onsuccess = () => resolve(r.result || null);
            r.onerror = () => resolve(null);
        });
    } catch (e) { return null; }
}
async function autosave() {
    if (! ready || ! dirty || playing || recording) return;
    try {
        const r = await callJ("atp_save_zip", "/autosave.zip");
        if (r.error) return;
        const bytes = await rpc({ op: "read", path: "/autosave.zip" });
        await remember(bytes);
        status("Autosaved in this browser · use Save .zip to keep a copy");
    } catch (e) {}
}
setInterval(autosave, 120000);

// ---------------------------------------------------------------- transport
async function togglePlay() {
    if (! ready) return;
    if (ctx.state !== "running") await ctx.resume();
    if (recording) { await stopRecording(); return; }
    if (playing) await call("atp_stop"); else await call("atp_play");
}
async function seek(seconds) { position = Math.max(0, seconds); if (ready) await call("atp_seek", position); drawTimeline(); }

async function toggleRecord() {
    if (! ready) return;
    if (recording) { await stopRecording(); return; }
    let stream;
    try {
        stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 } });
    } catch (e) { alertError("The browser didn't allow the microphone/input: " + e.message); return; }
    if (ctx.state !== "running") await ctx.resume();
    let trackId = selectedTrack;
    const track = project.tracks.find(t => t.id === trackId);
    if (! track || track.kind !== "audio") {
        const n = project.tracks.length + 1;
        const desc = await callJ("atp_add_track", "Audio " + n);
        setProject(desc, true);
        trackId = desc.tracks[desc.tracks.length - 1].id;
        selectedTrack = trackId;
    }
    const source = ctx.createMediaStreamSource(stream);
    source.connect(node);
    const settings = stream.getAudioTracks()[0].getSettings();
    recording = { trackId, start: position, chunks: [], stream, source, inputLatency: settings.latency || 0 };
    node.port.postMessage({ op: "record", on: true });
    await call("atp_play");
    $("recBtn").classList.add("on");
    status("Recording… (Space or R to stop)");
}

async function stopRecording() {
    const rec = recording;
    recording = null;
    node.port.postMessage({ op: "record", on: false });
    await call("atp_stop");
    rec.source.disconnect();
    rec.stream.getTracks().forEach(t => t.stop());
    $("recBtn").classList.remove("on");
    const frames = rec.chunks.reduce((n, c) => n + c[0].length, 0);
    // what was played came out of the speakers late and went into the input late: the take is moved earlier by both
    const latency = Math.round(((ctx.outputLatency || 0) + (ctx.baseLatency || 0) + rec.inputLatency) * ctx.sampleRate);
    if (frames - latency < ctx.sampleRate * 0.05) { status("Recording too short: nothing added"); return; }
    const L = new Float32Array(frames - latency), R = new Float32Array(frames - latency);
    let o = -latency;
    for (const [l, r] of rec.chunks) {
        for (let i = 0; i < l.length; ++i, ++o) if (o >= 0) { L[o] = l[i]; R[o] = r[i]; }
    }
    const takes = project.tracks.reduce((n, t) => n + t.regions.length, 0) + 1;
    const path = await uniquePath("Take " + takes + ".wav");
    const bytes = encodeWav([L, R], ctx.sampleRate);
    await rpc({ op: "write", path, data: bytes }, [bytes]);
    const desc = await callJ("atp_add_region", rec.trackId, path, rec.start);
    setProject(desc, true);
    changed();
    await seek(rec.start);
    status("Recorded " + (L.length / ctx.sampleRate).toFixed(1) + " s");
}

// ---------------------------------------------------------------- headers
function buildHeaders() {
    const list = $("headerList");
    list.innerHTML = "";
    project.tracks.forEach((t, i) => {
        const el = document.createElement("div");
        el.className = "th" + (t.id === selectedTrack ? " sel" : "");
        el.dataset.id = t.id;
        el.innerHTML = `<div class="sw" style="background:${colourOf(t, i)}"></div><div class="body">
            <div class="top"><input class="nm" spellcheck="false"><button class="ms m" title="Mute">M</button><button class="ms s" title="Solo">S</button><button class="x" title="Delete track">✕</button></div>
            <div class="vol"><input type="range" min="-60" max="6" step="0.1"><span class="dbv"></span></div>
            <div class="hm"><div></div></div></div>`;
        const nm = el.querySelector(".nm");
        nm.value = t.name;
        nm.onchange = async () => { await call("atp_set_track_name", t.id, nm.value); changed(); refresh(); };
        nm.onkeydown = e => { e.stopPropagation(); if (e.key === "Enter" || e.key === "Escape") nm.blur(); };
        el.querySelector(".m").classList.toggle("on", t.mute);
        el.querySelector(".s").classList.toggle("on", t.solo);
        el.querySelector(".m").onclick = () => setTrack(t, "mute", ! t.mute);
        el.querySelector(".s").onclick = () => setTrack(t, "solo", ! t.solo);
        el.querySelector(".x").onclick = async () => {
            if (! confirm("Delete the track \"" + t.name + "\"?")) return;
            await call("atp_remove_track", t.id); changed(); refresh();
        };
        const vol = el.querySelector(".vol input"), dbv = el.querySelector(".dbv");
        vol.value = toDb(t.volume); dbv.textContent = fmtDb(toDb(t.volume));
        vol.oninput = () => { dbv.textContent = fmtDb(+vol.value); setTrack(t, "volume", fromDb(+vol.value), false); };
        vol.ondblclick = () => { vol.value = 0; vol.oninput(); };
        el.onmousedown = e => { if (e.target.tagName !== "INPUT" && e.target.tagName !== "BUTTON") selectTrack(t.id); };
        list.appendChild(el);
    });
}

async function setTrack(t, key, value, rebuild = true) {
    t[key] = value;
    await call("atp_set_track", t.id, key, typeof value === "boolean" ? (value ? 1 : 0) : value);
    changed();
    if (rebuild) { buildHeaders(); buildMixer(); }
    else syncControls(t);
}
function syncControls(t) {
    for (const el of document.querySelectorAll(`[data-id="${t.id}"]`)) {
        const vol = el.querySelector(".vol input, .fader"), dbv = el.querySelector(".dbv, .db");
        if (vol && document.activeElement !== vol) vol.value = toDb(t.volume);
        if (dbv) dbv.textContent = fmtDb(toDb(t.volume));
        const pan = el.querySelector(".pan");
        if (pan && document.activeElement !== pan) pan.value = t.pan;
    }
}
function selectTrack(id) {
    selectedTrack = id;
    for (const el of document.querySelectorAll(".th")) el.classList.toggle("sel", el.dataset.id === id);
}

// ---------------------------------------------------------------- mixer
function buildMixer() {
    const strips = $("strips");
    strips.innerHTML = "";
    project.tracks.forEach((t, i) => {
        const el = document.createElement("div");
        el.className = "strip";
        el.dataset.id = t.id;
        el.style.setProperty("--c", colourOf(t, i));
        el.innerHTML = `<div class="inserts"></div>
            <input type="range" class="pan" min="-1" max="1" step="0.01" title="Pan (double-click: centre)"><div class="panv"></div>
            <div class="body2"><div class="meter"><canvas width="22" height="160"></canvas></div>
              <input type="range" class="fader" min="-60" max="6" step="0.1"></div>
            <div class="db"></div>
            <div class="ms-row"><button class="ms m">M</button><button class="ms s">S</button></div>
            <div class="name" title="${escapeHtml(t.name)}">${escapeHtml(t.name)}</div>`;
        fillInserts(el.querySelector(".inserts"), t.id, t.inserts);
        const pan = el.querySelector(".pan"), panv = el.querySelector(".panv");
        const showPan = () => { const p = +pan.value; panv.textContent = Math.abs(p) < 0.01 ? "C" : (p < 0 ? "L" : "R") + Math.round(Math.abs(p) * 100); };
        pan.value = t.pan; showPan();
        pan.oninput = () => { showPan(); setTrack(t, "pan", +pan.value, false); };
        pan.ondblclick = () => { pan.value = 0; pan.oninput(); };
        const fader = el.querySelector(".fader"), dbv = el.querySelector(".db");
        fader.value = toDb(t.volume); dbv.textContent = fmtDb(toDb(t.volume));
        fader.oninput = () => { dbv.textContent = fmtDb(+fader.value); setTrack(t, "volume", fromDb(+fader.value), false); };
        fader.ondblclick = () => { fader.value = 0; fader.oninput(); };
        el.querySelector(".m").classList.toggle("on", t.mute);
        el.querySelector(".s").classList.toggle("on", t.solo);
        el.querySelector(".m").onclick = () => setTrack(t, "mute", ! t.mute);
        el.querySelector(".s").onclick = () => setTrack(t, "solo", ! t.solo);
        el.querySelector(".name").onclick = () => selectTrack(t.id);
        strips.appendChild(el);
    });
    fillInserts($("masterInserts"), "master", project.masterInserts || []);
    const mf = $("masterFader");
    mf.value = toDb(project.masterGain);
    $("masterDb").textContent = fmtDb(toDb(project.masterGain));
}
function fillInserts(box, owner, inserts) {
    box.innerHTML = "";
    for (const fx of inserts) {
        const b = document.createElement("button");
        b.className = "ins" + (fx.plugin ? " plugin" : fx.bypass ? " off" : "");
        b.textContent = fx.name;
        b.title = fx.plugin ? fx.name + ": a plugin. It plays in the desktop app and stays in the project."
                            : fx.name + " (click to open · Alt-click: bypass)";
        b.onclick = async e => {
            if (fx.plugin) { status(b.title); return; }
            if (e.altKey) { fx.bypass = ! fx.bypass; await call("atp_set_bypass", fx.id, fx.bypass ? 1 : 0); changed(); b.classList.toggle("off", fx.bypass); return; }
            openEffect(fx.id);
        };
        box.appendChild(b);
    }
    const add = document.createElement("button");
    add.className = "ins add";
    add.textContent = "+";
    add.title = "Add an effect";
    add.onclick = e => showEffectMenu(owner, e.clientX, e.clientY);
    box.appendChild(add);
}

let effectList = null;
async function showEffectMenu(owner, x, y) {
    if (! ready) return;
    if (! effectList) effectList = await callJ("atp_effects");
    const menu = $("fxMenu");
    menu.innerHTML = "";
    const cats = [];
    for (const e of effectList) if (! cats.includes(e.category)) cats.push(e.category);
    for (const c of cats) {
        const h = document.createElement("div"); h.className = "cat"; h.textContent = c; menu.appendChild(h);
        for (const e of effectList.filter(e => e.category === c)) {
            const it = document.createElement("div"); it.className = "item"; it.textContent = e.name;
            it.onclick = async () => {
                menu.hidden = true;
                const r = await callJ("atp_add_effect", owner, e.type);
                if (r.error) { alertError(r.error); return; }
                changed();
                await refresh();
                openEffect(r.id);
            };
            menu.appendChild(it);
        }
    }
    menu.hidden = false;
    const w = menu.offsetWidth, h = menu.offsetHeight;
    menu.style.left = Math.max(8, Math.min(x, innerWidth - w - 8)) + "px";
    menu.style.top = Math.max(8, Math.min(y, innerHeight - h - 8)) + "px";
}
addEventListener("mousedown", e => { if (! $("fxMenu").hidden && ! $("fxMenu").contains(e.target)) $("fxMenu").hidden = true; });

let openFx = null;
async function openEffect(id) {
    const info = await callJ("atp_effect_params", id);
    if (info.error) { status(info.error); return; }
    openFx = id;
    $("fxTitle").textContent = info.name;
    $("fxBypass").classList.toggle("on", info.bypass);
    const box = $("fxParams");
    box.innerHTML = "";
    for (const p of info.params) {
        const d = document.createElement("div");
        d.className = "param";
        const label = document.createElement("label");
        label.innerHTML = `<span>${escapeHtml(p.name)}</span><b></b>`;
        d.appendChild(label);
        const out = label.querySelector("b");
        let input;
        if (p.choices.length) {
            input = document.createElement("select");
            p.choices.forEach((c, i) => { const o = document.createElement("option"); o.value = i; o.textContent = c; input.appendChild(o); });
            input.value = Math.round(p.value);
            input.onchange = () => setParam(p, +input.value);
        } else {
            // the slider is shaped so its centre is the knob's mid point (as the desktop's knobs)
            const skew = p.mid > p.min && p.mid < p.max ? Math.log(0.5) / Math.log((p.mid - p.min) / (p.max - p.min)) : 1;
            const toPos = v => Math.pow(Math.max(0, (v - p.min) / (p.max - p.min)), skew);
            const toVal = t => p.min + (p.max - p.min) * Math.pow(t, 1 / skew);
            input = document.createElement("input");
            input.type = "range"; input.min = 0; input.max = 1; input.step = 0.001;
            input.value = toPos(p.value);
            const show = v => out.textContent = fmtParam(v, p);
            show(p.value);
            input.oninput = () => { const v = toVal(+input.value); show(v); setParam(p, v); };
            input.ondblclick = () => { input.value = toPos(p.def); input.oninput(); };
        }
        d.appendChild(input);
        box.appendChild(d);
    }
    $("fxPanel").hidden = false;
}
function fmtParam(v, p) {
    const a = Math.abs(v);
    const n = a >= 1000 ? (v / 1000).toFixed(2) + "k" : a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2);
    return n + (p.suffix || "");
}
async function setParam(p, v) { if (openFx) { await call("atp_set_effect_param", openFx, p.id, v); changed(); } }
$("fxClose").onclick = () => { $("fxPanel").hidden = true; openFx = null; };
$("fxBypass").onclick = async () => {
    if (! openFx) return;
    const on = ! $("fxBypass").classList.contains("on");
    $("fxBypass").classList.toggle("on", on);
    await call("atp_set_bypass", openFx, on ? 1 : 0);
    changed(); refresh();
};
$("fxRemove").onclick = async () => {
    if (! openFx) return;
    await call("atp_remove_effect", openFx);
    $("fxPanel").hidden = true; openFx = null;
    changed(); refresh();
};
(() => {   // the effect window moves by its title bar
    const head = document.querySelector(".fx-head"), panel = $("fxPanel");
    head.addEventListener("mousedown", e => {
        if (e.target.tagName === "BUTTON") return;
        const r = panel.getBoundingClientRect(), dx = e.clientX - r.left, dy = e.clientY - r.top;
        const move = ev => { panel.style.transform = "none"; panel.style.left = (ev.clientX - dx) + "px"; panel.style.top = Math.max(0, ev.clientY - dy) + "px"; };
        const up = () => { removeEventListener("mousemove", move); removeEventListener("mouseup", up); };
        addEventListener("mousemove", move); addEventListener("mouseup", up);
    });
})();

function escapeHtml(s) { return String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c])); }

function drawMeter(canvas, l, r) {
    let st = meterState.get(canvas);
    if (! st) { st = { l: -90, r: -90, holdL: -90, holdR: -90, tl: 0, tr: 0 }; meterState.set(canvas, st); }
    const now = performance.now();
    const dbL = l > 0 ? 20 * Math.log10(l) : -90, dbR = r > 0 ? 20 * Math.log10(r) : -90;
    st.l = Math.max(dbL, st.l - 1.2); st.r = Math.max(dbR, st.r - 1.2);   // ~36 dB a second fall
    if (dbL >= st.holdL || now - st.tl > 2000) { st.holdL = dbL; st.tl = now; }
    if (dbR >= st.holdR || now - st.tr > 2000) { st.holdR = dbR; st.tr = now; }
    const g = canvas.getContext("2d"), w = canvas.width, h = canvas.height, bw = (w - 3) / 2;
    const y = db => h - Math.max(0, Math.min(1, (db + 60) / 66)) * h;
    g.fillStyle = "#0b0f10"; g.fillRect(0, 0, w, h);
    [[st.l, st.holdL, 1], [st.r, st.holdR, 2 + bw]].forEach(([v, hold, x]) => {
        const top = y(v);
        const grad = g.createLinearGradient(0, h, 0, 0);
        grad.addColorStop(0, "#45c25a"); grad.addColorStop(0.75, "#45c25a"); grad.addColorStop(0.84, "#e8c53c"); grad.addColorStop(0.92, "#e2463f");
        g.fillStyle = grad;
        g.fillRect(x, top, bw, h - top);
        if (hold > -60) { g.fillStyle = hold > -0.1 ? "#ff5a50" : "#e8e9ec"; g.fillRect(x, y(hold) - 1, bw, 2); }
    });
    g.fillStyle = "rgba(255,255,255,.18)";
    for (const mark of [0, -6, -12, -24, -48]) g.fillRect(0, Math.round(y(mark)), w, 1);
}

// ---------------------------------------------------------------- arrange window
const lanes = $("lanes"), timeline = $("timeline"), ruler = $("ruler");
const spacer = document.createElement("div");
spacer.style.cssText = "position:absolute;left:0;top:0;width:1px;height:1px;pointer-events:none";
lanes.appendChild(spacer);
timeline.style.position = "sticky"; timeline.style.left = "0"; timeline.style.top = "0";

function contentWidth() { return Math.max(lanes.clientWidth, (Math.max(project.length, 60) + 30) * pxPerSec); }
function layout() {
    const dpr = window.devicePixelRatio || 1;
    spacer.style.width = contentWidth() + "px";
    spacer.style.height = Math.max(lanes.clientHeight, project.tracks.length * TRACK_H() + 40) + "px";
    timeline.width = lanes.clientWidth * dpr; timeline.height = lanes.clientHeight * dpr;
    timeline.style.width = lanes.clientWidth + "px"; timeline.style.height = lanes.clientHeight + "px";
    timeline.style.marginBottom = -lanes.clientHeight + "px";
    ruler.width = ruler.clientWidth * dpr; ruler.height = ruler.clientHeight * dpr;
    drawTimeline();
}
window.addEventListener("resize", layout);
lanes.addEventListener("scroll", () => { $("headerList").scrollTop = lanes.scrollTop; drawTimeline(); });

const beatSeconds = () => 60 / (project.tempo || 120);
function gridStep() {   // seconds between grid lines that are at least ~12 px apart
    const beat = beatSeconds();
    for (const s of [beat / 4, beat / 2, beat, beat * 2, beat * 4, beat * 8, beat * 16, beat * 32]) if (s * pxPerSec >= 12) return s;
    return beat * 64;
}

function drawTimeline() {
    const dpr = window.devicePixelRatio || 1, g = timeline.getContext("2d");
    const W = timeline.width / dpr, H = timeline.height / dpr, sx = lanes.scrollLeft, sy = lanes.scrollTop, th = TRACK_H();
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = "#1e2829"; g.fillRect(0, 0, W, H);
    project.tracks.forEach((t, i) => {
        const y = i * th - sy;
        if (y > H || y + th < 0) return;
        g.fillStyle = t.id === selectedTrack ? "#25323f" : (i % 2 ? "#252f30" : "#212b2c");
        g.fillRect(0, y, W, th);
        g.fillStyle = "#1a2223"; g.fillRect(0, y + th - 1, W, 1);
    });
    // grid
    const step = gridStep(), bar = beatSeconds() * 4;
    for (let s = Math.floor(sx / pxPerSec / step) * step; s * pxPerSec - sx < W; s += step) {
        const x = Math.round(s * pxPerSec - sx) + 0.5, isBar = Math.abs(s / bar - Math.round(s / bar)) < 1e-6;
        g.fillStyle = isBar ? "rgba(70,82,83,.9)" : "rgba(46,56,57,.9)";
        g.fillRect(x, 0, 1, H);
    }
    // regions
    project.tracks.forEach((t, i) => {
        const y = i * th - sy;
        if (y > H || y + th < 0) return;
        const col = colourOf(t, i);
        for (const r of t.regions) {
            const moving = drag && drag.region === r.id;
            const start = moving ? drag.start : r.start;
            const row = moving ? drag.row : i;
            drawRegion(g, r, col, start * pxPerSec - sx, row * th - sy + 3, r.length * pxPerSec, th - 7, W);
        }
    });
    // the take being recorded
    if (recording) {
        const i = project.tracks.findIndex(t => t.id === recording.trackId);
        const len = Math.max(0, position - recording.start);
        g.fillStyle = "rgba(226,70,63,.55)";
        g.fillRect(recording.start * pxPerSec - sx, i * th - sy + 3, len * pxPerSec, th - 7);
    }
    // playhead
    const px = Math.round(position * pxPerSec - sx) + 0.5;
    g.fillStyle = "#e8e9ec"; g.fillRect(px, 0, 1, H);
    drawRuler();
}

function drawRegion(g, r, col, x, y, w, h, W) {
    if (x > W || x + w < 0) return;
    const sel = r.id === selectedRegion;
    g.save();
    g.globalAlpha = r.muted ? 0.4 : 1;
    g.fillStyle = shade(col, 0.42);
    roundRect(g, x, y, w, h, 4); g.fill();
    g.fillStyle = col;
    g.fillRect(x, y, w, 14);
    g.strokeStyle = sel ? "#ffffff" : shade(col, 0.75); g.lineWidth = sel ? 2 : 1;
    roundRect(g, x + 0.5, y + 0.5, w - 1, h - 1, 4); g.stroke();
    g.beginPath(); g.rect(x, y, w, h); g.clip();
    g.fillStyle = "#10181a"; g.font = "600 11px -apple-system, Segoe UI, sans-serif";
    g.fillText((r.missing ? "⚠ " : "") + (r.name || "Region") + (r.muted ? " (muted)" : ""), x + 5, y + 11);
    const top = y + 15, mid = top + (h - 15) / 2, half = (h - 17) / 2;
    if (r.midi) {
        g.fillStyle = shade(col, 1.2); g.font = "11px sans-serif"; g.fillText("MIDI", x + 5, mid + 4);
    } else if (r.missing) {
        g.fillStyle = "#ffb4ae"; g.font = "11px sans-serif"; g.fillText("Audio file missing (not in the zip)", x + 5, mid + 4);
    } else {
        const wf = waveforms.get(r.id);
        if (wf) {
            const pts = wf.length / 2, x0 = Math.max(0, -x), x1 = Math.min(w, W - x);
            g.fillStyle = shade(col, 1.35);
            for (let px = Math.floor(x0); px < x1; ++px) {
                const a = Math.floor(px / w * pts), b = Math.max(a + 1, Math.floor((px + 1) / w * pts));
                let lo = 0, hi = 0;
                for (let k = a; k < b && k < pts; ++k) { lo = Math.min(lo, wf[k * 2]); hi = Math.max(hi, wf[k * 2 + 1]); }
                const y0 = mid - hi * half, y1 = mid - lo * half;
                g.fillRect(x + px, y0, 1, Math.max(1, y1 - y0));
            }
        }
    }
    g.restore();
}
function roundRect(g, x, y, w, h, r) {
    r = Math.min(r, w / 2, h / 2);
    g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r);
    g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath();
}
function shade(hex, k) {
    const n = parseInt(hex.slice(1), 16);
    const c = [n >> 16 & 255, n >> 8 & 255, n & 255].map(v => Math.max(0, Math.min(255, Math.round(k > 1 ? v + (255 - v) * (k - 1) : v * k))));
    return "rgb(" + c.join(",") + ")";
}

function drawRuler() {
    const dpr = window.devicePixelRatio || 1, g = ruler.getContext("2d"), W = ruler.width / dpr, H = ruler.height / dpr, sx = lanes.scrollLeft;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = "#364042"; g.fillRect(0, 0, W, H);
    const bar = beatSeconds() * 4;
    let every = 1;
    while (bar * every * pxPerSec < 46) every *= 2;
    g.font = "11px -apple-system, Segoe UI, sans-serif";
    for (let b = Math.floor(sx / pxPerSec / bar / every) * every; b * bar * pxPerSec - sx < W; b += every) {
        const x = Math.round(b * bar * pxPerSec - sx) + 0.5;
        g.fillStyle = "#7d8a8b"; g.fillRect(x, H - 12, 1, 12);
        g.fillStyle = "#c9d0d2"; g.fillText(String(b + 1), x + 3, H - 4);
    }
    const px = position * pxPerSec - sx;
    g.fillStyle = "#e8e9ec";
    g.beginPath(); g.moveTo(px - 6, 0); g.lineTo(px + 6, 0); g.lineTo(px, 9); g.closePath(); g.fill();
}

function followPlayhead() {
    const x = position * pxPerSec;
    if (x > lanes.scrollLeft + lanes.clientWidth * 0.9 || x < lanes.scrollLeft) lanes.scrollLeft = Math.max(0, x - lanes.clientWidth * 0.1);
}

function zoom(factor, anchorClientX) {
    const rect = lanes.getBoundingClientRect();
    const ax = anchorClientX !== undefined ? anchorClientX - rect.left : position * pxPerSec - lanes.scrollLeft;
    const t = (lanes.scrollLeft + ax) / pxPerSec;
    pxPerSec = Math.max(2, Math.min(2000, pxPerSec * factor));
    spacer.style.width = contentWidth() + "px";
    lanes.scrollLeft = Math.max(0, t * pxPerSec - ax);
    drawTimeline();
}

// seeking on the ruler
ruler.addEventListener("mousedown", e => {
    const at = ev => seek((ev.clientX - ruler.getBoundingClientRect().left + lanes.scrollLeft) / pxPerSec);
    at(e);
    const move = ev => at(ev), up = () => { removeEventListener("mousemove", move); removeEventListener("mouseup", up); };
    addEventListener("mousemove", move); addEventListener("mouseup", up);
});

// selecting and moving regions
let drag = null;
timeline.addEventListener("mousedown", e => {
    const rect = timeline.getBoundingClientRect(), th = TRACK_H();
    const x = e.clientX - rect.left + lanes.scrollLeft, y = e.clientY - rect.top + lanes.scrollTop;
    const row = Math.floor(y / th), t = project.tracks[row], secs = x / pxPerSec;
    if (! t) { selectedRegion = null; drawTimeline(); return; }
    selectTrack(t.id);
    const r = t.regions.slice().reverse().find(r => secs >= r.start && secs <= r.start + r.length);
    selectedRegion = r ? r.id : null;
    if (! r) { if (e.detail === 2) seek(secs); drawTimeline(); return; }
    drag = { region: r.id, grab: secs - r.start, start: r.start, row, origRow: row, origStart: r.start, moved: false };
    const move = ev => {
        const xx = ev.clientX - rect.left + lanes.scrollLeft, yy = ev.clientY - rect.top + lanes.scrollTop;
        let s = Math.max(0, xx / pxPerSec - drag.grab);
        if (! ev.altKey) { const q = gridStep(); s = Math.round(s / q) * q; }
        drag.start = s;
        drag.row = Math.max(0, Math.min(project.tracks.length - 1, Math.floor(yy / th)));
        drag.moved = drag.moved || Math.abs(s - drag.origStart) > 1e-6 || drag.row !== drag.origRow;
        drawTimeline();
    };
    const up = async () => {
        removeEventListener("mousemove", move); removeEventListener("mouseup", up);
        const d = drag;
        drag = null;
        if (! d.moved) { drawTimeline(); return; }
        const target = project.tracks[d.row];
        if (target && d.row !== d.origRow) await call("atp_move_region_to_track", d.region, target.id);
        await call("atp_set_region", d.region, "start", d.start);
        changed();
        await refresh();
    };
    addEventListener("mousemove", move); addEventListener("mouseup", up);
});
timeline.addEventListener("dblclick", async e => {
    if (! selectedRegion) return;
    const f = findRegion(selectedRegion);
    if (f) { await call("atp_set_region", f.region.id, "mute", f.region.muted ? 0 : 1); changed(); refresh(); }
});
lanes.addEventListener("wheel", e => {
    if (e.ctrlKey || e.metaKey) { e.preventDefault(); zoom(e.deltaY < 0 ? 1.15 : 1 / 1.15, e.clientX); }
}, { passive: false });

// ---------------------------------------------------------------- the frame: meters, clock, playhead
function frame() {
    const secs = position, bt = beatSeconds();
    const beats = secs / bt;
    $("posBars").textContent = (Math.floor(beats / 4) + 1) + "." + (Math.floor(beats) % 4 + 1) + "." + (Math.floor((beats % 1) * 4) + 1);
    $("posTime").textContent = Math.floor(secs / 60) + ":" + (secs % 60).toFixed(3).padStart(6, "0");
    drawTimeline();
    if (levels.length >= 2) {
        drawMeter($("masterStrip").querySelector("canvas"), levels[0], levels[1]);
        if (! $("mixer").hidden) {
            const canvases = $("strips").querySelectorAll("canvas");
            canvases.forEach((c, i) => drawMeter(c, levels[2 + i * 2] || 0, levels[3 + i * 2] || 0));
        }
        const bars = document.querySelectorAll(".th .hm div");
        bars.forEach((b, i) => {
            const v = Math.max(levels[2 + i * 2] || 0, levels[3 + i * 2] || 0), db = v > 0 ? 20 * Math.log10(v) : -90;
            b.style.width = Math.max(0, Math.min(100, (db + 60) / 66 * 100)) + "%";
            b.style.background = db > -0.1 ? "#e2463f" : db > -6 ? "#e8c53c" : "#45c25a";
        });
    }
    requestAnimationFrame(frame);
}

// ---------------------------------------------------------------- controls
function status(text) { $("status").textContent = text; }
function alertError(text) { status(text); alert(text); }
function hideOverlay() { $("startOverlay").hidden = true; }

$("newBtn").onclick = newProject;
$("startNew").onclick = newProject;
$("openBtn").onclick = $("startOpen").onclick = async () => { if (! ready) await startEngine(); $("zipInput").click(); };
$("zipInput").onchange = e => { if (e.target.files[0]) openZip(e.target.files[0]); e.target.value = ""; };
$("importBtn").onclick = async () => { if (! ready) await startEngine(); $("audioInput").click(); };
$("audioInput").onchange = e => { importFiles([...e.target.files]); e.target.value = ""; };
$("saveBtn").onclick = saveZip;
$("exportBtn").onclick = exportWav;
$("playBtn").onclick = togglePlay;
$("homeBtn").onclick = () => seek(0);
$("recBtn").onclick = toggleRecord;
$("zoomIn").onclick = () => zoom(1.5);
$("zoomOut").onclick = () => zoom(1 / 1.5);
$("mixerBtn").onclick = () => { $("mixer").hidden = ! $("mixer").hidden; $("mixerBtn").classList.toggle("on", ! $("mixer").hidden); layout(); };
$("addTrackBtn").onclick = async () => { if (! ready) return; setProject(await callJ("atp_add_track", "Audio " + (project.tracks.length + 1)), true); changed(); };
$("masterFader").oninput = async () => {
    const db = +$("masterFader").value;
    $("masterDb").textContent = fmtDb(db);
    project.masterGain = fromDb(db);
    await call("atp_set_master", project.masterGain);
    changed();
};
$("masterFader").ondblclick = () => { $("masterFader").value = 0; $("masterFader").oninput(); };

addEventListener("keydown", async e => {
    if ((e.target.tagName === "INPUT" && e.target.type !== "range") || e.target.tagName === "SELECT") return;
    if (e.key === "Escape") { $("fxPanel").hidden = true; $("fxMenu").hidden = true; openFx = null; return; }
    if (e.key === " ") { e.preventDefault(); togglePlay(); }
    else if (e.key === "Enter") { e.preventDefault(); seek(0); }
    else if (e.key === "r" || e.key === "R") { if (! e.metaKey && ! e.ctrlKey) toggleRecord(); }
    else if (e.key === "x" || e.key === "X") { if (! e.metaKey && ! e.ctrlKey) $("mixerBtn").click(); }
    else if (e.key === "+" || e.key === "=") zoom(1.5);
    else if (e.key === "-") zoom(1 / 1.5);
    else if ((e.key === "Delete" || e.key === "Backspace") && selectedRegion) {
        e.preventDefault();
        await call("atp_set_region", selectedRegion, "delete", 1);
        selectedRegion = null; changed(); refresh();
    }
    else if ((e.metaKey || e.ctrlKey) && e.key === "s") { e.preventDefault(); saveZip(); }
});

// drag and drop
let dragDepth = 0;
addEventListener("dragenter", e => { e.preventDefault(); if (++dragDepth === 1) document.body.classList.add("dragging"); });
addEventListener("dragleave", e => { if (--dragDepth <= 0) { dragDepth = 0; document.body.classList.remove("dragging"); } });
addEventListener("dragover", e => e.preventDefault());
addEventListener("drop", async e => {
    e.preventDefault();
    dragDepth = 0; document.body.classList.remove("dragging");
    const files = [...e.dataTransfer.files];
    if (! files.length) return;
    if (! ready) await startEngine();
    if ($("startOverlay").hidden === false && ! files.some(f => f.name.toLowerCase().endsWith(".zip"))) {
        setProject(await callJ("atp_new_project", "Untitled"));
        hideOverlay();
    }
    importFiles(files);
});
addEventListener("beforeunload", e => { if (dirty) { e.preventDefault(); e.returnValue = ""; } });

// the last saved project in this browser
recalled().then(last => {
    if (! last) return;
    const b = document.createElement("button");
    b.textContent = "Reopen \"" + (last.name || "Untitled") + "\" (" + new Date(last.when).toLocaleString() + ")";
    b.onclick = async () => {
        await startEngine();
        await openZip(new File([last.bytes], (last.name || "Untitled") + ".zip"));
    };
    $("startOverlay").querySelector(".row").appendChild(b);
});

wasmBytes.catch(e => status("Couldn't load the engine: " + e.message));
status("Choose New Project or Open to start");
layout();
