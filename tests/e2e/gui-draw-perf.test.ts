/**
 * End-to-end benchmark for the two immediate-mode drawing surfaces.
 *
 * The `gui-perf` suite measures the retained DOM pipeline (restyle/layout/paint
 * runs). This one measures the two APIs a multi-element scene actually calls
 * thousands of times per frame:
 *
 *   - `<canvas>` 2D: solid `fillRect`, multi-segment `stroke`, repeated
 *     `fillText` and 1:1 `drawImage`, drawn into a 1280x720 bitmap;
 *   - WebGL: 2000 `drawElements` calls with a per-draw matrix, plus a 64 KB
 *     `bufferSubData` into a 4 MB buffer.
 *
 * The programs print wall-clock milliseconds per scene (informational: CI may
 * run on a software GPU) and the engine counters that a regression would move:
 * `canvasDraws`/`canvasUploads`/`canvasUploadBytes` and
 * `webglDraws`/`webglPasses`/`webglPipelineMisses`/`webglBufferUploadBytes`
 * from `win.stats()` (see `runtime/ext_gui/perf.h`).
 *
 * The counters are asserted (deterministic). Timings are printed and, on a
 * developer machine, can be bounded with `XBINTSC_GUI_DRAW_PERF_STRICT_MS`.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/** Frames each scene runs after the first (which does the one-time GPU work). */
const FRAMES = 16;

type Fields = Record<string, number>;

/** Parse `bench.<name> key=value …` lines into numbers. */
function parseBenches(stdout: string): Record<string, Fields> {
  const benches: Record<string, Fields> = {};
  for (const line of stdout.split("\n")) {
    const match = /^bench\.([a-z0-9]+)[ \t]+(.*)$/.exec(line);
    if (match === null) continue;
    const name = match[1];
    const rest = match[2];
    if (name === undefined || rest === undefined) continue;
    const fields: Fields = {};
    for (const token of rest.split(/\s+/)) {
      const equals = token.indexOf("=");
      if (equals <= 0) continue;
      const value = Number(token.slice(equals + 1));
      if (!Number.isNaN(value)) fields[token.slice(0, equals)] = value;
    }
    /* A line like `bench.webgl unsupported` carries no fields; do not let it
     * replace the sample line of the same scene. */
    if (Object.keys(fields).length === 0) continue;
    benches[name] = fields;
  }
  return benches;
}

function field(benches: Record<string, Fields>, bench: string, key: string): number {
  const fields = benches[bench];
  if (fields === undefined) throw new Error(`missing bench.${bench} line in the program output`);
  const value = fields[key];
  if (value === undefined) throw new Error(`missing bench.${bench}.${key}`);
  return value;
}

/** Optional wall-clock budget for the canvas scene, in milliseconds per frame. */
function strictMs(): number {
  return Number(process.env.XBINTSC_GUI_DRAW_PERF_STRICT_MS ?? "0");
}

describe.skipIf(!guiAvailable)("gui extension — immediate-mode draw performance", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("draws many canvas primitives with bounded per-element overhead", () => {
    const result = harness.compileAndRun(
      "drawbench-canvas",
      `
      import { createWindow, run } from "gui";

      const W = 1280;
      const H = 720;
      const FRAMES = ${FRAMES};

      const HTML = "<html><head><style>body { margin: 0; }</style></head><body>" +
        "<canvas id='c' width='" + W + "' height='" + H + "'></canvas>" +
        "<canvas id='s' width='32' height='32'></canvas></body></html>";

      const win = createWindow({ title: "drawbench-canvas", width: 900, height: 700 });
      win.loadHTML(HTML);

      const round = (value: number): number => Math.round(value * 100) / 100;
      const colors = [
        "#ffffff", "#ff0000", "#00ff00", "#0000ff",
        "#ffff00", "#00ffff", "#ff00ff", "#888888",
        "#ff8800", "#88ff00", "#0088ff", "#ff0088",
        "#440000", "#004400", "#000044", "#cccccc",
      ];
      const labels = [
        "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
        "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
        "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey", "xray",
      ];

      /** Deterministic LCG so every run draws the same scene. */
      let seed = 12345;
      const rnd = (): number => {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        return seed / 2147483648;
      };
      const at = (span: number): number => Math.floor(rnd() * span);

      const canvas: any = win.document.getElementById("c");
      const ctx: any = canvas.getContext("2d");
      const sprite: any = win.document.getElementById("s");
      const sctx: any = sprite.getContext("2d");
      sctx.fillStyle = "#88ccff";
      sctx.fillRect(0, 0, 32, 32);

      const scene = (): void => {
        const fillStart = performance.now();
        /* 5000 small solid rectangles, a different colour per element. */
        for (let i = 0; i < 5000; i++) {
          ctx.fillStyle = colors[i % 16];
          ctx.fillRect(at(W - 8), at(H - 8), 8, 8);
        }
        const strokeStart = performance.now();
        /* 200 polylines of 8 points each (1400 segments). */
        ctx.strokeStyle = "#3399ff";
        ctx.lineWidth = 2;
        for (let p = 0; p < 200; p++) {
          ctx.beginPath();
          ctx.moveTo(at(W), at(H));
          for (let k = 0; k < 7; k++) ctx.lineTo(at(W), at(H));
          ctx.stroke();
        }
        const textStart = performance.now();
        /* 300 text runs reusing 24 labels, so shape/glyph caches can hit. */
        ctx.font = "12px sans-serif";
        ctx.fillStyle = "#eeeeee";
        for (let t = 0; t < 300; t++) {
          ctx.fillText(labels[t % 24], at(W - 120), at(H - 20));
        }
        const imageStart = performance.now();
        /* 200 1:1 sprite blits. */
        for (let im = 0; im < 200; im++) {
          ctx.drawImage(sprite, at(W - 32), at(H - 32));
        }
        const end = performance.now();
        fillMs = fillMs + (strokeStart - fillStart);
        strokeMs = strokeMs + (textStart - strokeStart);
        textMs = textMs + (imageStart - textStart);
        imageMs = imageMs + (end - imageStart);
      };

      const start = win.stats();
      let frame = 0;
      let fillMs = 0;
      let strokeMs = 0;
      let textMs = 0;
      let imageMs = 0;

      const report = (): void => {
        const end = win.stats();
        const draws = end.canvasDraws - start.canvasDraws;
        const uploads = end.canvasUploads - start.canvasUploads;
        const bytes = end.canvasUploadBytes - start.canvasUploadBytes;
        console.log("bench.canvas ms=" + round((fillMs + strokeMs + textMs + imageMs) / FRAMES) +
          " fill=" + round(fillMs / FRAMES) +
          " stroke=" + round(strokeMs / FRAMES) +
          " text=" + round(textMs / FRAMES) +
          " image=" + round(imageMs / FRAMES) +
          " draws=" + draws +
          " uploads=" + uploads +
          " bytes=" + bytes +
          " frames=" + FRAMES);
        win.close();
      };

      const tick = (): void => {
        scene();
        frame = frame + 1;
        if (frame < FRAMES) {
          win.requestAnimationFrame(tick);
          return;
        }
        /* Report one frame later: the counters of the last scene are only
         * folded into the totals when that frame ends. */
        win.requestAnimationFrame(report);
      };

      win.on("ready", () => win.requestAnimationFrame(tick));
      win.on("close", () => console.log("bench.canvas closed"));
      run();
      `,
      { autocloseMs: 20000, console: true },
    );

    expect(result.stdout).toContain("bench.canvas closed");
    const benches = parseBenches(result.stdout);
    console.log("canvas draw perf: " + result.stdout.split("\n").find((line) => line.startsWith("bench.canvas")));
    /* 5000 fillRects + 200 strokes + 300 fillTexts + 200 drawImages per frame. */
    expect(field(benches, "canvas", "draws")).toBe(5700 * FRAMES);
    /* Every drawn frame re-uploads the changed bitmap once — not K times for
     * K canvases once the uploads share the frame's command buffer. */
    expect(field(benches, "canvas", "uploads")).toBeGreaterThanOrEqual(FRAMES);
    expect(field(benches, "canvas", "uploads")).toBeLessThanOrEqual(FRAMES + 2);
    expect(field(benches, "canvas", "bytes")).toBeGreaterThan(0);

    const budget = strictMs();
    if (budget > 0) expect(field(benches, "canvas", "ms")).toBeLessThan(budget);
  }, 120000);

  it("uploads only the dirty rectangle of a canvas", () => {
    const result = harness.compileAndRun(
      "drawbench-dirty",
      `
      import { createWindow, run } from "gui";

      const W = 640;
      const H = 480;
      const FRAMES = 8;

      const HTML = "<html><head><style>body { margin: 0; }</style></head><body>" +
        "<canvas id='c' width='" + W + "' height='" + H + "'></canvas></body></html>";
      const win = createWindow({ title: "drawbench-dirty", width: 400, height: 400 });
      win.loadHTML(HTML);

      const canvas: any = win.document.getElementById("c");
      const ctx: any = canvas.getContext("2d");
      /* One full-canvas paint first: its upload is not part of the sample. */
      ctx.fillStyle = "#123456";
      ctx.fillRect(0, 0, W, H);

      let frame = 0;
      let start: any = null;

      const report = (): void => {
        const end = win.stats();
        console.log("bench.dirty bytes=" + (end.canvasUploadBytes - start.canvasUploadBytes) +
          " uploads=" + (end.canvasUploads - start.canvasUploads) +
          " frames=" + (FRAMES - 1));
        win.close();
      };

      const tick = (): void => {
        if (frame === 1) start = win.stats();
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(10, 10, 100, 100);
        frame = frame + 1;
        if (frame < FRAMES) {
          win.requestAnimationFrame(tick);
          return;
        }
        win.requestAnimationFrame(report);
      };

      win.on("ready", () => win.requestAnimationFrame(tick));
      win.on("close", () => console.log("bench.dirty closed"));
      run();
      `,
      { autocloseMs: 10000, console: true },
    );

    expect(result.stdout).toContain("bench.dirty closed");
    const benches = parseBenches(result.stdout);
    console.log(
      "canvas dirty upload: " +
        result.stdout.split("\n").find((line) => line.startsWith("bench.dirty")),
    );
    const frames = 8 - 1;
    expect(field(benches, "dirty", "uploads")).toBe(frames);
    /* 100x100x4 bytes per frame, not the whole 640x480 bitmap. */
    expect(field(benches, "dirty", "bytes")).toBeLessThanOrEqual(
      100 * 100 * 4 * frames + 4 * frames,
    );
    expect(field(benches, "dirty", "bytes")).toBeGreaterThan(0);
  }, 120000);

  it("issues many WebGL draws without restarting the pass or the pipeline", () => {
    const result = harness.compileAndRun(
      "drawbench-webgl",
      `
      import { createWindow, run } from "gui";

      const HTML = "<html><head><style>body { margin: 0; }</style></head><body>" +
        "<canvas id='c' width='512' height='512'></canvas></body></html>";

      const win = createWindow({ title: "drawbench-webgl", width: 700, height: 700 });
      win.loadHTML(HTML);

      const round = (value: number): number => Math.round(value * 100) / 100;

      const start = (): void => {
      const canvas: any = win.document.getElementById("c");
      const gl: any = canvas.getContext("webgl");
      if (gl == null) {
        console.log("bench.webgl unsupported");
        win.close();
        return;
      }

      const vs: any = gl.createShader(gl.VERTEX_SHADER);
      gl.shaderSource(vs, "attribute vec3 a_pos; uniform mat4 u_mvp;" +
        "void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }");
      gl.compileShader(vs);
      const fs: any = gl.createShader(gl.FRAGMENT_SHADER);
      gl.shaderSource(fs, "precision mediump float; uniform vec3 u_color;" +
        "void main() { gl_FragColor = vec4(u_color, 1.0); }");
      gl.compileShader(fs);
      const program: any = gl.createProgram();
      gl.attachShader(program, vs);
      gl.attachShader(program, fs);
      gl.linkProgram(program);
      gl.useProgram(program);

      const positions = new Float32Array([
        -5, -5, -5,   5, -5, -5,   5,  5, -5,  -5,  5, -5,
        -5, -5,  5,   5, -5,  5,   5,  5,  5,  -5,  5,  5,
      ]);
      const indices = new Uint16Array([
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
        0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7,
        1, 5, 6, 1, 6, 2, 0, 3, 7, 0, 7, 4,
      ]);
      const vbo: any = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
      gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
      const pos: any = gl.getAttribLocation(program, "a_pos");
      gl.enableVertexAttribArray(pos);
      gl.vertexAttribPointer(pos, 3, gl.FLOAT, false, 0, 0);
      const ibo: any = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
      const mvpLocation: any = gl.getUniformLocation(program, "u_mvp");
      const colorLocation: any = gl.getUniformLocation(program, "u_color");

      /* A 4 MiB dynamic buffer, patched with 64 KiB per frame. */
      const big = new Float32Array(1024 * 1024);
      const bigBuffer: any = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, bigBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, big, gl.DYNAMIC_DRAW);
      const patch = new Float32Array(16384);
      for (let i = 0; i < patch.length; i++) patch[i] = i % 7;

      const matrix = new Float32Array(16);
      matrix[0] = 0.05;
      matrix[5] = 0.05;
      matrix[10] = 0.05;
      matrix[15] = 1.0;

      const scene = (): void => {
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        for (let i = 0; i < 2000; i++) {
          matrix[12] = ((i % 40) - 20) * 0.045;
          matrix[13] = (Math.floor(i / 40) - 20) * 0.045;
          gl.uniformMatrix4fv(mvpLocation, false, matrix);
          gl.uniform3f(colorLocation, 1.0, 0.5, 0.2);
          gl.drawElements(gl.TRIANGLES, 36, gl.UNSIGNED_SHORT, 0);
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, bigBuffer);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, patch);
      };

      let frame = 0;
      let drawMs = 0;
      let start: any = null;

      const report = (): void => {
        const end = win.stats();
        const frames = ${FRAMES} - 1;
        console.log("bench.webgl ms=" + round(drawMs / ${FRAMES}) +
          " draws=" + (end.webglDraws - start.webglDraws) +
          " passes=" + (end.webglPasses - start.webglPasses) +
          " misses=" + (end.webglPipelineMisses - start.webglPipelineMisses) +
          " bytes=" + (end.webglBufferUploadBytes - start.webglBufferUploadBytes) +
          " frames=" + frames);
        win.close();
      };

      const tick = (): void => {
        /* The first frame creates the pipeline and uploads the two buffers;
         * sampling after it keeps those one-time costs out of the counters. */
        if (frame === 1) start = win.stats();
        const t0 = performance.now();
        scene();
        drawMs = drawMs + (performance.now() - t0);
        frame = frame + 1;
        if (frame < ${FRAMES}) {
          win.requestAnimationFrame(tick);
          return;
        }
        /* Report one frame later, so the last scene's counters have been
         * folded into the frame totals. */
        win.requestAnimationFrame(report);
      };

      win.requestAnimationFrame(tick);
      };

      win.on("ready", start);
      win.on("close", () => console.log("bench.webgl closed"));
      run();
      `,
      { autocloseMs: 30000, console: true },
    );

    expect(result.stdout).toContain("bench.webgl closed");
    if (result.stdout.includes("bench.webgl unsupported")) {
      console.log("webgl draw perf: unsupported on this backend");
      return;
    }
    const benches = parseBenches(result.stdout);
    console.log("webgl draw perf: " + result.stdout.split("\n").find((line) => line.startsWith("bench.webgl")));
    const frames = FRAMES - 1;
    expect(field(benches, "webgl", "draws")).toBe(2000 * frames);
    /* Warm-up created the pipeline; steady state must not create more. */
    expect(field(benches, "webgl", "misses")).toBe(0);
    /* One clear + one draw pass per frame, not one pass per draw call. */
    expect(field(benches, "webgl", "passes")).toBeGreaterThan(0);
    expect(field(benches, "webgl", "passes")).toBeLessThanOrEqual(2 * frames);
    /* Only the patched range is re-uploaded, not the whole 4 MiB buffer. */
    expect(field(benches, "webgl", "bytes")).toBeLessThanOrEqual(65536 * frames + 4 * frames);

    const budget = strictMs();
    if (budget > 0) expect(field(benches, "webgl", "ms")).toBeLessThan(budget);
  }, 120000);
});
