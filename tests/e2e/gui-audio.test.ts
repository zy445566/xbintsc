/**
 * End-to-end tests for Web Audio in the `gui` extension.
 *
 * The context is created with the private `{ __offline: true }` option, which
 * keeps SDL out of the test and makes `ctx.__render(frames)` return the mixed
 * audio synchronously. That is the same graph and renderer the SDL callback
 * drives, so these tests pin the DSP (sources, gain, AudioParam automation,
 * mixing) without needing an audio device.
 *
 * `decodeAudioData` is exercised with a WAV generated inside the program, both
 * to prove the in-engine decoder and to feed a decoded buffer back into the
 * mixer.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { findRuntimeLibrary } from "../../src/driver/runtime-lib.js";
import { mediaExtension } from "../../src/extensions/media/index.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

/* A 20 ms 440 Hz Ogg/Vorbis tone. The engine's WAV decoder rejects it, so the
 * test exercises the FFmpeg memory decoder behind the optional media extension.
 * Stored as base64 because the test work directory only takes text files. */
const fixtureDir = join(dirname(fileURLToPath(import.meta.url)), "fixtures");
const oggFixture = join(fixtureDir, "tiny-ogg.b64");
const OGG_BASE64 = existsSync(oggFixture) ? readFileSync(oggFixture, "utf8").trim() : "";
const mediaAvailable = findRuntimeLibrary("media") !== undefined;

const WAV_HELPERS = `
  function makeWav(samples: number[]): ArrayBuffer {
    const frames = samples.length;
    const dataBytes = frames * 2;
    const buffer = new ArrayBuffer(44 + dataBytes);
    const bytes = new Uint8Array(buffer);
    const view = new DataView(buffer);
    const tag = (offset: number, text: string): void => {
      for (let i = 0; i < text.length; i++) bytes[offset + i] = text.charCodeAt(i);
    };
    tag(0, "RIFF");
    view.setUint32(4, 36 + dataBytes, true);
    tag(8, "WAVE");
    tag(12, "fmt ");
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true);       // PCM
    view.setUint16(22, 1, true);       // mono
    view.setUint32(24, 48000, true);   // sample rate
    view.setUint32(28, 96000, true);   // byte rate
    view.setUint16(32, 2, true);       // block align
    view.setUint16(34, 16, true);      // bits per sample
    tag(36, "data");
    view.setUint32(40, dataBytes, true);
    for (let i = 0; i < frames; i++) {
      view.setInt16(44 + i * 2, Math.round(samples[i] * 32767), true);
    }
    return buffer;
  }
`;

describe.skipIf(!guiAvailable)("gui extension — Web Audio", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("shuts an open context down before SDL quits", () => {
    const { value } = harness.compileAndRun(
      "audio-exit",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "audio-exit", width: 200, height: 150 });
      win.on("ready", () => {
        const ctx = new AudioContext();
        const osc = ctx.createOscillator();
        osc.connect(ctx.destination);
        osc.start();
        window.setTimeout(() => {
          console.log("state=" + ctx.state);
          win.close();
        }, 100);
      });
      win.loadHTML("<div>audio</div>");
      run();
      `,
      { autocloseMs: 2000 },
    );
    // The window closed with the graph still live: the engine must destroy the
    // SDL audio stream before SDL_Quit, or the process crashes on exit.
    expect(value("state")).toBe("running");
  });

  it("exposes the standard AudioContext surface", () => {
    const { value } = harness.compileAndRun(
      "audio-surface",
      `
      async function main(): Promise<void> {
        const ctx = new AudioContext({ __offline: true });
        const factories = ["createGain", "createOscillator", "createBufferSource",
          "createBiquadFilter", "createDynamicsCompressor", "createStereoPanner",
          "createConstantSource", "createDelay", "createBuffer", "decodeAudioData",
          "resume", "suspend", "close"];
        let present = 0;
        for (const name of factories) if (typeof ctx[name] === "function") present += 1;
        console.log("api=" + ctx.sampleRate + ":" + ctx.state + ":" +
          (ctx.destination != null) + ":" + present + "/" + factories.length);

        const gain = ctx.createGain();
        const methods = ["setValueAtTime", "linearRampToValueAtTime",
          "exponentialRampToValueAtTime", "setTargetAtTime",
          "cancelScheduledValues", "cancelAndHoldAtTime"];
        let paramMethods = 0;
        for (const name of methods) if (typeof gain.gain[name] === "function") paramMethods += 1;
        console.log("param=" + gain.gain.value + ":" + gain.gain.defaultValue + ":" +
          paramMethods + "/" + methods.length + ":" + (typeof gain.connect) + ":" +
          (typeof gain.disconnect) + ":" + (typeof gain.start) + ":" + (typeof gain.stop));

        const before = ctx.currentTime;
        await ctx.suspend();
        console.log("suspend=" + ctx.state);
        await ctx.resume();
        console.log("resume=" + ctx.state);
        await ctx.close();
        console.log("close=" + ctx.state + ":" + (ctx.currentTime === before));
      }
      main();
      `,
      { console: true },
    );

    expect(value("api")).toBe("48000:running:true:13/13");
    expect(value("param")).toBe("1:1:6/6:function:function:function:function");
    expect(value("suspend")).toBe("suspended");
    expect(value("resume")).toBe("running");
    expect(value("close")).toBe("closed:true");
  });

  it("renders constant sources, stereo panners and delays", () => {
    const { value } = harness.compileAndRun(
      "audio-nodes",
      `
      const ctx = new AudioContext({ __offline: true });
      const constant = ctx.createConstantSource();
      constant.offset.value = 0.5;
      constant.connect(ctx.destination);
      constant.start();
      const body = ctx.__render(4);
      console.log("constant=" + body[0].toFixed(3) + ":" + body[1].toFixed(3));

      const panned = new AudioContext({ __offline: true });
      const left = panned.createConstantSource();
      left.offset.value = 0.5;
      const panner = panned.createStereoPanner();
      panner.pan.value = -1;
      left.connect(panner);
      panner.connect(panned.destination);
      left.start();
      const pan = panned.__render(4);
      console.log("panner=" + pan[0].toFixed(3) + ":" + pan[1].toFixed(3));

      const delayed = new AudioContext({ __offline: true });
      const source = delayed.createConstantSource();
      source.offset.value = 0.5;
      const delay = delayed.createDelay(1);
      delay.delayTime.value = 1 / 48000;
      source.connect(delay);
      delay.connect(delayed.destination);
      source.start();
      const echo = delayed.__render(8);
      console.log("delay=" + echo[0].toFixed(3) + ":" + echo[2].toFixed(3));
      `,
      { console: true },
    );

    expect(value("constant")).toBe("0.500:0.500");
    expect(value("panner")).toBe("0.500:0.000");
    expect(value("delay")).toBe("0.000:0.500");
  });

  it("decodes a WAV into a real AudioBuffer", () => {
    const { value } = harness.compileAndRun(
      "audio-decode",
      `
      ${WAV_HELPERS}

      async function main(): Promise<void> {
        const ctx = new AudioContext({ __offline: true });
        const decoded = await ctx.decodeAudioData(makeWav([0.5, -0.5, 0.25, 0.25]));
        const channel = decoded.getChannelData(0);
        console.log("format=" + decoded.numberOfChannels + ":" + decoded.length + ":" +
          decoded.sampleRate + ":" + Math.round(decoded.duration * 48000));
        console.log("samples=" + channel[0].toFixed(3) + "," + channel[1].toFixed(3) +
          "," + channel[2].toFixed(3));
        console.log("channel=" + (channel instanceof Float32Array) + ":" + channel.length);

        // A typed-array view (not just a bare ArrayBuffer) is accepted too.
        const view = new Uint8Array(makeWav([0.5, -0.5, 0.25, 0.25]));
        const fromView = await ctx.decodeAudioData(view);
        console.log("view=" + fromView.length + ":" +
          fromView.getChannelData(0)[0].toFixed(3));
      }
      main();
      `,
      { console: true },
    );

    expect(value("format")).toBe("1:4:48000:4");
    expect(value("samples")).toBe("0.500,-0.500,0.250");
    expect(value("channel")).toBe("true:4");
    expect(value("view")).toBe("4:0.500");
  });

  it("mixes sources and a gain into the destination", () => {
    const { value } = harness.compileAndRun(
      "audio-mix",
      `
      function constantSource(ctx: any, value: number): any {
        const buffer = ctx.createBuffer(1, 256, 48000);
        const data = buffer.getChannelData(0);
        for (let i = 0; i < data.length; i++) data[i] = value;
        const source = ctx.createBufferSource();
        source.buffer = buffer;
        return source;
      }

      // One source through a gain: 0.5 * 0.25 = 0.125 in both channels.
      const ctx = new AudioContext({ __offline: true });
      const source = constantSource(ctx, 0.5);
      const gain = ctx.createGain();
      gain.gain.value = 0.25;
      source.connect(gain);
      gain.connect(ctx.destination);
      source.start(0);
      const soft = ctx.__render(64);
      console.log("gain=" + soft.length + ":" + soft[0].toFixed(3) + ":" +
        soft[1].toFixed(3) + ":" + soft[126].toFixed(3));

      // Two sources sum at the destination: 0.5 + 0.5 = 1.0.
      const sum = new AudioContext({ __offline: true });
      const first = constantSource(sum, 0.5);
      first.connect(sum.destination);
      first.start(0);
      const second = constantSource(sum, 0.5);
      second.connect(sum.destination);
      second.start(0);
      const both = sum.__render(8);
      console.log("sum=" + both[0].toFixed(3) + ":" + both[14].toFixed(3));

      // Stereo buffer: left 0.5, right -0.25.
      const stereo = new AudioContext({ __offline: true });
      const buffer = stereo.createBuffer(2, 64, 48000);
      const left = buffer.getChannelData(0);
      const right = buffer.getChannelData(1);
      for (let i = 0; i < left.length; i++) { left[i] = 0.5; right[i] = -0.25; }
      const player = stereo.createBufferSource();
      player.buffer = buffer;
      player.connect(stereo.destination);
      player.start(0);
      const channels = stereo.__render(4);
      console.log("stereo=" + channels[0].toFixed(3) + ":" + channels[1].toFixed(3));
      `,
      { console: true },
    );

    expect(value("gain")).toBe("128:0.125:0.125:0.125");
    expect(value("sum")).toBe("1.000:1.000");
    expect(value("stereo")).toBe("0.500:-0.250");
  });

  it("renders oscillator waveforms and AudioParam automation", () => {
    const { value } = harness.compileAndRun(
      "audio-params",
      `
      // A 480 Hz sine at 48 kHz advances by 1/100 of a cycle per frame.
      const ctx = new AudioContext({ __offline: true });
      const osc = ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.value = 480;
      osc.connect(ctx.destination);
      osc.start();
      const sine = ctx.__render(4);
      console.log("sine=" + sine[0].toFixed(4) + ":" + sine[2].toFixed(4) + ":" +
        sine[4].toFixed(4) + ":" + sine[6].toFixed(4));

      // A linear ramp from 0 to 1 over 48 frames, on a constant 0.5 source.
      const rampCtx = new AudioContext({ __offline: true });
      const buffer = rampCtx.createBuffer(1, 128, 48000);
      const data = buffer.getChannelData(0);
      for (let i = 0; i < data.length; i++) data[i] = 0.5;
      const source = rampCtx.createBufferSource();
      source.buffer = buffer;
      const gain = rampCtx.createGain();
      gain.gain.setValueAtTime(0, 0);
      gain.gain.linearRampToValueAtTime(1, 48 / 48000);
      source.connect(gain);
      gain.connect(rampCtx.destination);
      source.start(0);
      const ramp = rampCtx.__render(64);
      console.log("ramp=" + ramp[0].toFixed(3) + ":" + ramp[96].toFixed(3) + ":" +
        ramp[126].toFixed(3));

      // currentTime follows the rendered quanta (128-frame granularity).
      console.log("clock=" + rampCtx.currentTime.toFixed(4));
      `,
      { console: true },
    );

    expect(value("sine")).toBe("0.0000:0.0628:0.1253:0.1874");
    expect(value("ramp")).toBe("0.000:0.500:0.500");
    expect(value("clock")).toBe("0.0027");
  });

  it.skipIf(!mediaAvailable || OGG_BASE64.length === 0)(
    "decodes compressed audio through the media extension",
    () => {
      const { value } = harness.compileAndRun(
        "audio-media",
        `
        import { Buffer } from "buffer";

        const OGG = ${JSON.stringify(OGG_BASE64)};

        async function main(): Promise<void> {
          const data: any = Buffer.from(OGG, "base64");
          const ctx: any = new AudioContext({ __offline: true });
          const decoded: any = await ctx.decodeAudioData(data);
          console.log("bytes=" + data.length);
          console.log("ogg=" + decoded.numberOfChannels + ":" + decoded.sampleRate + ":" +
            (decoded.length > 50));
        }
        main();
        `,
        { extensions: [mediaExtension, nodeExtension], console: true },
      );

      expect(value("bytes")).toBe("2707");
      expect(value("ogg")).toBe("1:48000:true");
    },
  );

  it("rejects bytes that are not a supported audio file", () => {
    const { value } = harness.compileAndRun(
      "audio-error",
      `
      async function main(): Promise<void> {
        const ctx = new AudioContext({ __offline: true });
        try {
          await ctx.decodeAudioData(new ArrayBuffer(16));
          console.log("error=none");
        } catch (e: any) {
          console.log("error=" + e.name + ":" + (e.message.indexOf("decode") >= 0));
        }
      }
      main();
      `,
      { console: true },
    );

    expect(value("error")).toBe("EncodingError:true");
  });
});
