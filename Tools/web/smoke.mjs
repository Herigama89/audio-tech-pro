// Smoke test of the web engine under Node: new project, add a tone, play, levels, export, save zip, reopen it.
//   node Tools/web/smoke.mjs <folder with atp-engine.js>
import { createRequire } from "module";
import path from "path";
const require = createRequire(import.meta.url);
const dir = path.resolve(process.argv[2]);
require(path.join(dir, "atp-engine.js"));
const M = await globalThis.createAtpEngine({ locateFile: f => path.join(dir, f) });

const str = s => { const n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n); M.stringToUTF8(s, p, n); return p; };
const call = (fn, ...args) => {
    const ptrs = [];
    const a = args.map(x => typeof x === "string" ? (ptrs.push(str(x)), ptrs[ptrs.length - 1]) : x);
    const r = M["_" + fn](...a);
    ptrs.forEach(p => M._free(p));
    return r;
};
const json = (fn, ...args) => JSON.parse(M.UTF8ToString(call(fn, ...args)));
const fail = m => { console.error("FAIL:", m); process.exit(1); };

// a 2-second 440 Hz stereo 16-bit WAV
function wav(seconds, rate = 48000) {
    const n = seconds * rate, b = Buffer.alloc(44 + n * 4);
    b.write("RIFF", 0); b.writeUInt32LE(36 + n * 4, 4); b.write("WAVE", 8); b.write("fmt ", 12);
    b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24);
    b.writeUInt32LE(rate * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(n * 4, 40);
    for (let i = 0; i < n; ++i) { const v = Math.round(16000 * Math.sin(2 * Math.PI * 440 * i / rate)); b.writeInt16LE(v, 44 + i * 4); b.writeInt16LE(v, 46 + i * 4); }
    return b;
}

call("atp_init", 48000);
let d = json("atp_new_project", "Web Test");
M.FS.writeFile("/project/tone.wav", wav(2));
d = json("atp_add_audio_track", "/project/tone.wav", 0.5);
if (d.error) fail(d.error);
if (d.tracks.length !== 1 || Math.abs(d.tracks[0].regions[0].length - 2) > 0.01) fail("track/region not added: " + JSON.stringify(d));
console.log("added:", d.tracks[0].name, d.tracks[0].regions[0]);

const wp = M._malloc(200 * 4);
if (call("atp_waveform", d.tracks[0].regions[0].id, wp, 100) !== 100) fail("no waveform");
const w = new Float32Array(M.HEAPF32.buffer, wp, 200);
console.log("waveform max", Math.max(...w).toFixed(3));

const L = M._malloc(128 * 4), R = M._malloc(128 * 4);
call("atp_seek", 0); call("atp_play");
let peak = 0, silentBefore = true;
for (let b = 0; b < 48000 * 1.5 / 128; ++b) {
    call("atp_process", L, R, 128);
    const l = new Float32Array(M.HEAPF32.buffer, L, 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
    if (b === 100 && peak > 0.0001) silentBefore = false;   // ~0.27 s: before the region
}
console.log("position", call("atp_position").toFixed(3), "peak", peak.toFixed(3));
if (! silentBefore) fail("sound before the region starts");
if (peak < 0.1) fail("no sound from the region");

const lv = M._malloc(64 * 4), nl = call("atp_levels", lv, 64);
console.log("levels", Array.from(new Float32Array(M.HEAPF32.buffer, lv, nl)).map(v => v.toFixed(3)).join(" "));

// built-in effects: listed, added, their knobs read and set, shown in the description
const fx = json("atp_effects");
if (fx.length < 20) fail("too few effects: " + fx.length);
const added = json("atp_add_effect", d.tracks[0].id, "eq");
if (added.error) fail(added.error);
const knobs = json("atp_effect_params", added.id);
if (! knobs.params.length) fail("no knobs");
call("atp_set_effect_param", added.id, knobs.params[0].id, knobs.params[0].max);
if (json("atp_effect_params", added.id).params[0].value !== knobs.params[0].max) fail("knob not set");
const master = json("atp_add_effect", "master", "compressor");
d = json("atp_describe");
if (d.tracks[0].inserts.length !== 1 || d.masterInserts.length !== 1) fail("inserts not described");
console.log("effects:", fx.length, "eq knobs:", knobs.params.length, "master:", d.masterInserts[0].name);
call("atp_seek", 0.6); call("atp_play");
peak = 0;
for (let b = 0; b < 200; ++b) { call("atp_process", L, R, 128); for (const v of new Float32Array(M.HEAPF32.buffer, L, 128)) peak = Math.max(peak, Math.abs(v)); }
call("atp_stop");
if (! (peak > 0.05)) fail("no sound through the effects: " + peak);
call("atp_remove_effect", master.id);
if (json("atp_describe").masterInserts.length !== 0) fail("effect not removed");

let r = json("atp_export_wav", "/mix.wav", 0, 3);
if (r.error) fail(r.error);
console.log("export bytes", M.FS.stat("/mix.wav").size);

call("atp_set_track_name", d.tracks[0].id, "Renamed");
r = json("atp_save_zip", "/copy.zip");
if (r.error) fail(r.error);
const zip = M.FS.readFile("/copy.zip");
console.log("zip bytes", zip.length);

const zp = M._malloc(zip.length); M.HEAPU8.set(zip, zp);
d = json("atp_open_zip", zp, zip.length);
if (d.error) fail(d.error);
console.log("reopened:", d.name, d.tracks.map(t => t.name), "missing", d.tracks[0].regions[0].missing);
if (d.tracks[0].name !== "Renamed" || d.tracks[0].regions[0].missing) fail("reopen lost data");
console.log("OK");
