// Prüft das WebAssembly-Modul in Node: gleiche Messungen wie die nativen Tests
import fs from "node:fs";
const bytes = fs.readFileSync(new URL("./dist/onno-dsp.wasm", import.meta.url));
let mem;
const stub = () => 0;
const { instance } = await WebAssembly.instantiate(bytes, {
  env: { emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: { fd_close: stub, fd_write: stub, fd_seek: stub },
});
const x = instance.exports;
mem = x.memory;
const str = (p) => { const b = new Uint8Array(mem.buffer, p); let e = 0; while (b[e]) e++; return new TextDecoder().decode(b.subarray(0, e)); };
let fails = 0;
const check = (what, got, want, tol) => { const ok = Math.abs(got - want) <= tol; if (!ok) fails++; console.log(`${ok ? "OK  " : "FEHL"} ${what.padEnd(46)} ${got.toFixed(2)} (soll ${want.toFixed(2)})`); };

function measure(kind, set, freq, amp, blocks = 400) {
  const params = JSON.parse(str(x.onno_params_json(kind)));
  const h = x.onno_create(kind, 48000, 128);
  for (const [id, v] of Object.entries(set)) x.onno_set(h, params.findIndex((p) => p.id === id), v);
  let t = 0, si = 0, so = 0;
  for (let k = 0; k < blocks; k++) {
    const L = new Float32Array(mem.buffer, x.onno_buffer(h, 0), 128), R = new Float32Array(mem.buffer, x.onno_buffer(h, 1), 128);
    let e = 0;
    for (let i = 0; i < 128; i++, t++) { const v = amp * Math.sin(2 * Math.PI * freq * t / 48000); L[i] = v; R[i] = v; e += v * v; }
    x.onno_process(h, 128, 2, 0, 0, 0);
    const L2 = new Float32Array(mem.buffer, x.onno_buffer(h, 0), 128);
    if (k >= blocks - 100) { si += e; for (let i = 0; i < 128; i++) so += L2[i] * L2[i]; }
  }
  x.onno_destroy(h);
  return 10 * Math.log10(so / si);
}

const eq = JSON.parse(str(x.onno_params_json(0)));
console.log(`EQ: ${eq.length} Parameter, Comp: ${JSON.parse(str(x.onno_params_json(1))).length} Parameter`);
check("EQ Bell +6 dB bei 1 kHz", measure(0, { b3freq: 1000, b3gain: 6 }, 1000, 0.25), 6, 0.05);
check("EQ Bell +6 dB, 100 Hz bleibt", measure(0, { b3freq: 1000, b3gain: 6 }, 100, 0.25), 0.07, 0.05);
check("EQ Output -6 dB", measure(0, { output: -6 }, 1000, 0.25), -6, 0.05);
check("Comp 1 kHz -6 dBFS (nativ: -8.26)", measure(1, {}, 1000, 0.5), -8.262, 0.02);
check("Comp unter Threshold", measure(1, { thr: -18, ratio: 4 }, 1000, 0.0316), 0, 0.05);
const h = x.onno_create(0, 48000, 128);
check("EQ-Kurve Bell +6 dB (Anzeige)", (x.onno_set(h, 15, 6), x.onno_set(h, 14, 1000), x.onno_eq_response(h, 1000)), 6, 0.01);
console.log(fails ? `${fails} FEHLER` : "WASM OK");
process.exit(fails ? 1 : 0);
