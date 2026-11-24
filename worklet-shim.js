// What the engine's loader expects that the browser's audio thread doesn't have.
if (typeof globalThis.performance === "undefined")
    globalThis.performance = { now: () => (typeof currentTime === "number" ? currentTime * 1000 : Date.now()) };
if (typeof globalThis.crypto === "undefined")
    globalThis.crypto = { getRandomValues: a => { for (let i = 0; i < a.length; ++i) a[i] = Math.floor(Math.random() * 256); return a; } };
