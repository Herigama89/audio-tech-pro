// The Audio Tech Pro engine (WebAssembly) on the browser's audio thread. The page talks to it by messages:
//   { id, op: "call", fn, args }      -> { id, result }        (an atp_* function: numbers and strings)
//   { id, op: "write", path, data }   -> { id, result: true }  (a file into the engine's memory)
//   { id, op: "read", path }          -> { id, result: bytes } (a file out of it)
//   { id, op: "waveform", region, points } -> { id, result: Float32Array }
// and it sends { type: "tick", position, playing, levels } about 30 times a second, and recorded input
// as { type: "input", left, right } while recording is on.

class EngineProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.ready = false;
        this.recording = false;
        this.inputChunk = null;
        this.inputFill = 0;
        this.sinceTick = 0;
        this.port.onmessage = e => this.handle(e.data);
        this.start(options.processorOptions);
    }

    async start({ wasm, rate }) {
        try {
            this.M = await globalThis.createAtpEngine({
                wasmBinary: wasm,
                print: t => this.port.postMessage({ type: "log", text: t }),
                printErr: t => this.port.postMessage({ type: "log", text: t }),
            });
            const M = this.M;
            M._atp_init(rate);
            this.left = M._malloc(128 * 4);
            this.right = M._malloc(128 * 4);
            this.levelsPtr = M._malloc(1024 * 4);
            this.ready = true;
            this.port.postMessage({ type: "ready" });
        } catch (err) {
            this.port.postMessage({ type: "failed", text: String(err && err.stack || err) });
        }
    }

    str(s) {
        const M = this.M, n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n);
        M.stringToUTF8(s, p, n);
        return p;
    }

    call(fn, args, returnsText) {
        const M = this.M, temps = [];
        const a = args.map(x => {
            if (typeof x === "string") { const p = this.str(x); temps.push(p); return p; }
            if (typeof x === "boolean") return x ? 1 : 0;
            return x;
        });
        const r = M["_" + fn](...a);
        temps.forEach(p => M._free(p));
        return returnsText ? JSON.parse(M.UTF8ToString(r)) : r;
    }

    handle(msg) {
        if (msg.op === "record") { this.recording = msg.on; this.inputChunk = null; return; }
        if (! this.ready) { this.port.postMessage({ id: msg.id, error: "The engine isn't ready yet." }); return; }
        const M = this.M;
        try {
            let result, transfer = [];
            switch (msg.op) {
                case "call":
                    result = this.call(msg.fn, msg.args || [], msg.text);
                    break;
                case "write": {
                    const dir = msg.path.substring(0, msg.path.lastIndexOf("/"));
                    if (dir) M.FS.mkdirTree(dir);
                    M.FS.writeFile(msg.path, new Uint8Array(msg.data));
                    result = true;
                    break;
                }
                case "exists":
                    result = M.FS.analyzePath(msg.path).exists;
                    break;
                case "read": {
                    const bytes = M.FS.readFile(msg.path);
                    result = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
                    transfer = [result];
                    try { M.FS.unlink(msg.path); } catch (e) {}
                    break;
                }
                case "openZip": {
                    const bytes = new Uint8Array(msg.data), p = M._malloc(bytes.length);
                    M.HEAPU8.set(bytes, p);
                    result = JSON.parse(M.UTF8ToString(M._atp_open_zip(p, bytes.length)));
                    M._free(p);
                    break;
                }
                case "waveform": {
                    const p = M._malloc(msg.points * 2 * 4), id = this.str(msg.region);
                    const n = M._atp_waveform(id, p, msg.points);
                    result = new Float32Array(M.HEAPF32.buffer, p, n * 2).slice();
                    transfer = [result.buffer];
                    M._free(p); M._free(id);
                    break;
                }
            }
            this.port.postMessage({ id: msg.id, result }, transfer);
        } catch (err) {
            this.port.postMessage({ id: msg.id, error: String(err && err.message || err) });
        }
    }

    process(inputs, outputs) {
        const out = outputs[0];
        if (! this.ready) return true;
        const M = this.M, frames = out[0].length;
        M._atp_process(this.left, this.right, frames);
        const heap = M.HEAPF32;   // (looked up each time: the memory can grow)
        out[0].set(heap.subarray(this.left >> 2, (this.left >> 2) + frames));
        if (out[1]) out[1].set(heap.subarray(this.right >> 2, (this.right >> 2) + frames));

        if (this.recording) this.capture(inputs[0], frames);

        this.sinceTick += frames;
        if (this.sinceTick >= sampleRate / 30) {
            this.sinceTick = 0;
            const n = M._atp_levels(this.levelsPtr, 1024);
            this.port.postMessage({
                type: "tick",
                position: M._atp_position(),
                playing: M._atp_is_playing() !== 0,
                levels: new Float32Array(M.HEAPF32.buffer, this.levelsPtr, n).slice(),
            });
        }
        return true;
    }

    capture(input, frames) {
        const size = 4096;
        if (! this.inputChunk) { this.inputChunk = [new Float32Array(size), new Float32Array(size)]; this.inputFill = 0; }
        const l = input && input[0], r = input && (input[1] || input[0]);
        for (let i = 0; i < frames; ++i) {
            this.inputChunk[0][this.inputFill] = l ? l[i] : 0;
            this.inputChunk[1][this.inputFill] = r ? r[i] : 0;
            if (++this.inputFill === size) {
                const [a, b] = this.inputChunk;
                this.port.postMessage({ type: "input", left: a, right: b }, [a.buffer, b.buffer]);
                this.inputChunk = [new Float32Array(size), new Float32Array(size)];
                this.inputFill = 0;
            }
        }
    }
}

registerProcessor("atp-engine", EngineProcessor);
