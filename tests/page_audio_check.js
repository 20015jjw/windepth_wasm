/* Run index.html's inline script under node against a stub Web Audio API, so
 * the page's audio plumbing is testable without a browser.
 *
 *   node tests/page_audio_check.js
 *
 * This exists because the first attempt at the page shipped a bug that no
 * amount of testing the wasm module could catch: the pump was created only if
 * the module happened to be ready at the moment of the first user gesture, and
 * because the AudioContext already existed every later call returned early.
 * Effects still played and the F4 label still toggled, so it looked fine.
 *
 * Both scripts are evaluated in the real global scope with vm.runInThisContext,
 * which is what a browser does with two <script> tags - and what the generated
 * windepth.js needs, since its `var Module = typeof Module != "undefined" ...`
 * only sees a pre-set Module at global scope.
 *
 * Pass --early to press the key BEFORE the wasm runtime comes up, which is the
 * order that used to leave the page silent for good.
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '..');

/* ------------------------------------------------------- Web Audio stubs */

class FakeBuffer {
    constructor(ch, len, rate) {
        this.numberOfChannels = ch;
        this.length = len;
        this.sampleRate = rate;
        this._d = [];
        for (let i = 0; i < ch; i++)
            this._d.push(new Float32Array(len));
    }
    getChannelData(i) { return this._d[i]; }
}

class FakeNode {
    connect() { return this; }
    disconnect() {}
}

class FakeGain extends FakeNode {
    constructor() { super(); this.gain = { value: 1 }; }
}

class FakeSource extends FakeNode {
    constructor(ctx) { super(); this.ctx = ctx; this.buffer = null; }
    start(t) {
        this.ctx.scheduled.push({ t: t === undefined ? this.ctx.currentTime : t,
                                  buffer: this.buffer });
    }
}

class FakeAudioContext {
    constructor() {
        this.sampleRate = 48000;
        this.state = 'running';
        this.currentTime = 0;
        this.scheduled = [];
        this.destination = new FakeNode();
        FakeAudioContext.last = this;   /* so the test can inspect it */
    }
    resume() { this.state = 'running'; return Promise.resolve(); }
    createGain() { return new FakeGain(); }
    createBuffer(ch, len, rate) { return new FakeBuffer(ch, len, rate); }
    createBufferSource() { return new FakeSource(this); }
    decodeAudioData(b, ok) { ok(new FakeBuffer(1, 1, this.sampleRate)); return Promise.resolve(); }
    createScriptProcessor() {
        throw new Error('createScriptProcessor should not be needed any more');
    }
}

/* ------------------------------------------------------------- DOM stubs */

const listeners = {};
const timers = [];
const elements = {};

function stubEl() {
    return new Proxy({ textContent: '', style: {},
                       addEventListener(type, fn) { listeners[type] = fn; },
                       /* no WebGL here, so the page takes its 2D path */
                       getContext(type) {
                           if (type !== '2d')
                               return null;
                           return {
                               createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }),
                               putImageData() {}
                           };
                       },
                       width: 512, height: 384 },
                     { get: (t, k) => (k in t ? t[k] : undefined),
                       set: (t, k, v) => { t[k] = v; return true; } });
}

globalThis.document = {
    getElementById(id) {
        if (!elements[id]) elements[id] = stubEl();
        return elements[id];
    }
};
globalThis.window = globalThis;
globalThis.AudioContext = FakeAudioContext;
globalThis.webkitAudioContext = FakeAudioContext;
globalThis.addEventListener = (type, fn) => { listeners[type] = fn; };
globalThis.fetch = url => {
    const p = path.join(root, url);
    return Promise.resolve({
        ok: true,
        arrayBuffer: () => Promise.resolve(fs.readFileSync(p).buffer)
    });
};
/* Collect the intervals instead of running them, so time can be driven. */
globalThis.setInterval = (fn, ms) => { timers.push({ fn, ms }); return timers.length; };
globalThis.clearInterval = () => {};
globalThis.setTimeout = (fn, ms) => realSetTimeout(fn, ms);
const realSetTimeout = require('timers').setTimeout;

/* windepth.js takes the node path when it sees process, so give it what that
 * path needs at global scope. */
globalThis.require = require;
globalThis.__dirname = root;
globalThis.__filename = path.join(root, 'windepth.js');
globalThis.module = { exports: {} };

/* ------------------------------------------------------------------- run */

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const inline = /<script>([\s\S]*?)<\/script>/.exec(html);
if (!inline) {
    console.error('no inline script in index.html');
    process.exit(1);
}
vm.runInThisContext(inline[1], { filename: 'index.html#inline' });
vm.runInThisContext(fs.readFileSync(path.join(root, 'windepth.js'), 'utf8'),
                    { filename: 'windepth.js' });

const early = process.argv.includes('--early');

(async () => {
    if (early) {
        /* The gesture lands while the module is still loading. */
        if (!listeners.keydown) {
            console.error('no keydown listener yet');
            process.exit(1);
        }
        listeners.keydown({ code: 'KeyZ', preventDefault() {} });
        console.log('early gesture sent before the runtime was up');
    }
    for (let i = 0; i < 500 && !globalThis.Module._wd_init; i++)
        await new Promise(r => realSetTimeout(r, 20));
    if (!globalThis.Module._wd_init) {
        console.error('wasm runtime never came up');
        process.exit(1);
    }
    /* onRuntimeInitialized has already run by now; a gesture has not. */
    if (!listeners.keydown) {
        console.error('the page registered no keydown listener');
        process.exit(1);
    }
    console.log('timers after init:', timers.length,
                timers.map(t => t.ms + 'ms').join(' '));

    /* The gesture that a real user makes: press a key. */
    if (!early)
        listeners.keydown({ code: 'KeyZ', preventDefault() {} });
    const ctx = findContext();
    if (!ctx) {
        console.error('no AudioContext was created by the gesture');
        process.exit(1);
    }
    console.log('after gesture: state', ctx.state, 'rate', ctx.sampleRate,
                'timers', timers.length);
    if (timers.length < 3) {
        console.error('FAIL: no music pump timer - the pump was never created');
        process.exit(1);
    }

    /* Drive past the Bio_100% splash (120 game ticks = 6 s) into the title.
     * Both are meant to be SILENT: the original only starts the sequencer when
     * a game starts.  See BGM_FROM_TITLE in src/game.c. */
    const step = 0.05;
    const SECONDS = 12;
    let firstSound = -1, mode = -1;

    for (let t = 0; t < SECONDS; t += step) {
        ctx.currentTime = t;
        const before = ctx.scheduled.length;

        for (const tm of timers)
            tm.fn();
        if (Module._wd_mode() !== mode) {
            mode = Module._wd_mode();
            console.log(`  t=${t.toFixed(2)}s mode -> ${mode}`);
        }
        if (firstSound < 0)
            for (let i = before; i < ctx.scheduled.length; i++) {
                const b = ctx.scheduled[i].buffer;
                if (!b) continue;
                const d = b.getChannelData(0);
                for (let k = 0; k < d.length; k += 7)
                    if (Math.abs(d[k]) > 0.01) { firstSound = t; break; }
                if (firstSound >= 0) break;
            }
    }
    console.log('attract screens: first audible block at t =',
                firstSound < 0 ? 'never (correct)' : firstSound.toFixed(2) + 's');
    if (firstSound >= 0) {
        console.error('FAIL: the splash and title screens should be silent');
        process.exit(1);
    }

    const sch = ctx.scheduled;
    let peak = 0, frames = 0;
    for (const s of sch) {
        if (!s.buffer) continue;
        frames += s.buffer.length;
        for (let c = 0; c < s.buffer.numberOfChannels; c++) {
            const d = s.buffer.getChannelData(c);
            for (let i = 0; i < d.length; i += 7)
                if (Math.abs(d[i]) > peak) peak = Math.abs(d[i]);
        }
    }
    console.log(`scheduled ${sch.length} buffers, ${frames} frames ` +
                `(${(frames / ctx.sampleRate).toFixed(2)}s), peak ${peak.toFixed(3)}`);

    /* Gapless: consecutive start times must differ by exactly one chunk. */
    let gaps = 0;
    const chunk = sch.length > 1 && sch[0].buffer ? sch[0].buffer.length : 0;
    for (let i = 1; i < sch.length; i++) {
        const d = sch[i].t - sch[i - 1].t;
        if (Math.abs(d - chunk / ctx.sampleRate) > 1e-6) gaps++;
    }
    console.log(`chunk ${chunk} frames, non-contiguous joins ${gaps}`);

    if (sch.length === 0) {
        console.error('FAIL: nothing was scheduled - the pump never ran');
        process.exit(1);
    }
    if (frames / ctx.sampleRate < SECONDS * 0.9) {
        console.error('FAIL: less than the wall clock was generated');
        process.exit(1);
    }

    /* Start a game: that is where the BGM comes in, from the top of the tune. */
    const play = drive('F2', 1.0);
    console.log(`after Start: peak ${play.toFixed(3)}`);
    if (play < 0.01) {
        console.error('FAIL: no music once a game started');
        process.exit(1);
    }

    /* F4 mutes and unmutes: the page path, not just the module export. */
    const off = drive('F4', 1.0);
    const on  = drive('F4', 1.0);
    console.log(`F4 off -> peak ${off.toFixed(3)}, F4 on -> peak ${on.toFixed(3)}`);
    if (off > 0.001) {
        console.error('FAIL: F4 did not silence the music');
        process.exit(1);
    }
    if (on < 0.01) {
        console.error('FAIL: F4 did not bring the music back');
        process.exit(1);
    }

    console.log('OK');
    process.exit(0);

    /* Send a key, run `secs` of wall clock, and return the peak of whatever was
     * scheduled during it. */
    function drive(code, secs) {
        const from = ctx.scheduled.length;
        let p = 0;

        listeners.keydown({ code, preventDefault() {} });
        for (let t = 0; t < secs; t += step) {
            ctx.currentTime += step;
            for (const tm of timers)
                tm.fn();
        }
        for (let i = from; i < ctx.scheduled.length; i++) {
            const b = ctx.scheduled[i].buffer;

            if (!b) continue;
            for (let c = 0; c < b.numberOfChannels; c++) {
                const d = b.getChannelData(c);
                for (let k = 0; k < d.length; k += 7)
                    if (Math.abs(d[k]) > p) p = Math.abs(d[k]);
            }
        }
        return p;
    }

    function findContext() {
        /* The page keeps the context in a module-scope local, so pick up the
         * one the stub constructor recorded. */
        return FakeAudioContext.last;
    }
})();
