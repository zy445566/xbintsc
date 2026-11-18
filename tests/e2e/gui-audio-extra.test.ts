/**
 * End-to-end tests for the Web Audio nodes added on top of the classic set:
 * WaveShaper, Convolver, Analyser, Panner, the media sources and
 * `AudioContext.listener`.
 *
 * Like `gui-audio.test.ts` these use the private offline context
 * (`new AudioContext({ __offline: true })`), so `ctx.__render(frames)` returns
 * the mixed samples synchronously and no audio device is involved. Every
 * expected value below is hand-computed from the DSP, so a regression in the
 * curve mapping, the partition bookkeeping or the panning math fails the test.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — Web Audio effects and sources", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("shapes through a WaveShaper curve with both sampler paths", () => {
    const { value } = harness.compileAndRun(
      "audio-shaper",
      `
      const ctx = new AudioContext({ __offline: true });
      /* [-0.5, 0, 0.5] over [-1, 1] is a *0.5 transfer curve: 0.5 in, 0.25 out. */
      const shaper = ctx.createWaveShaper();
      shaper.curve = new Float32Array([-0.5, 0, 0.5]);
      const source = ctx.createConstantSource();
      source.offset.value = 0.5;
      source.connect(shaper);
      shaper.connect(ctx.destination);
      source.start();
      const shaped = ctx.__render(256);
      console.log("shape=" + shaped[0].toFixed(4) + ":" + shaped[1].toFixed(4));
      console.log("curve=" + shaper.curve.length + ":" + shaper.oversample);

      shaper.oversample = "4x";
      const again = ctx.__render(256);
      console.log("oversampled=" + again[0].toFixed(4) + ":" + shaper.oversample);

      /* A curve shorter than two entries is a pass-through, per the platform. */
      const ctx2 = new AudioContext({ __offline: true });
      const plain = ctx2.createWaveShaper();
      plain.curve = new Float32Array([1]);
      const second = ctx2.createConstantSource();
      second.offset.value = 0.25;
      second.connect(plain);
      plain.connect(ctx2.destination);
      second.start();
      const passthrough = ctx2.__render(256);
      console.log("short=" + passthrough[0].toFixed(4));
      `,
      { console: true, autocloseMs: 6000 },
    );
    expect(value("shape")).toBe("0.2500:0.2500");
    expect(value("curve")).toBe("3:none");
    expect(value("oversampled")).toBe("0.2500:4x");
    expect(value("short")).toBe("0.2500");
  });

  it("convolves with a partitioned impulse response", () => {
    const { value } = harness.compileAndRun(
      "audio-convolver",
      `
      const ctx = new AudioContext({ __offline: true });
      /* A unit impulse passes the signal through unchanged when the response is
       * not normalized: this pins the partition indexing and the overlap-save
       * output window. */
      const impulse = ctx.createBuffer(1, 512, 48000);
      impulse.getChannelData(0)[0] = 1;
      const convolver = ctx.createConvolver();
      convolver.normalize = false;
      convolver.buffer = impulse;
      const source = ctx.createConstantSource();
      source.offset.value = 0.25;
      source.connect(convolver);
      convolver.connect(ctx.destination);
      source.start();
      const first = ctx.__render(256);
      console.log("impulse=" + first[0].toFixed(4) + ":" + first[1].toFixed(4));
      console.log("normalize=" + convolver.normalize + ":buffer=" + (convolver.buffer === impulse));

      /* A unit impulse two frames in shifts the output by two frames: the
       * samples before it stay silent, which pins the history window. */
      const shifted = ctx.createBuffer(1, 512, 48000);
      shifted.getChannelData(0)[2] = 1;
      convolver.buffer = shifted;
      const second = ctx.__render(256);
      console.log("delayed=" + second[4].toFixed(4) + ":" + second[0].toFixed(4));

      /* Clearing the buffer makes the convolver silent, as on the platform. */
      convolver.buffer = null;
      const silent = ctx.__render(256);
      console.log("cleared=" + silent[0].toFixed(4));
      `,
      { console: true, autocloseMs: 6000 },
    );
    expect(value("impulse")).toBe("0.2500:0.2500");
    expect(value("normalize")).toBe("false:buffer=true");
    expect(value("delayed")).toBe("0.2500:0.0000");
    expect(value("cleared")).toBe("0.0000");
  });

  it("analyses a tone and attenuates a panned panner", () => {
    const { value } = harness.compileAndRun(
      "audio-analysis",
      `
      const ctx = new AudioContext({ __offline: true });
      const osc = ctx.createOscillator();
      osc.frequency.value = 1000;
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 1024;
      osc.connect(analyser);
      analyser.connect(ctx.destination);
      osc.start();
      ctx.__render(8192);
      console.log("fft=" + analyser.fftSize + ":" + analyser.frequencyBinCount);
      const bytes = new Uint8Array(analyser.frequencyBinCount);
      analyser.getByteFrequencyData(bytes);
      let peak = 0;
      let peakBin = 0;
      for (let i = 0; i < bytes.length; i++) if (bytes[i] > peak) { peak = bytes[i]; peakBin = i; }
      /* 1000 Hz at 48000 / 1024 bins lands around bin 21 (46.9 Hz per bin). */
      console.log("peak=" + (peakBin >= 20 && peakBin <= 22) + ":" + (peak > 0));
      const time = new Float32Array(analyser.fftSize);
      analyser.getFloatTimeDomainData(time);
      let loudest = 0;
      for (let i = 0; i < time.length; i++) if (Math.abs(time[i]) > loudest) loudest = Math.abs(time[i]);
      console.log("time=" + (loudest > 0.9));
      analyser.smoothingTimeConstant = 0.25;
      console.log("smoothing=" + analyser.smoothingTimeConstant);

      const ctx2 = new AudioContext({ __offline: true });
      const panner = ctx2.createPanner();
      panner.panningModel = "HRTF";
      panner.distanceModel = "inverse";
      panner.refDistance = 1;
      panner.rolloffFactor = 1;
      panner.setPosition(0, 0, -8);
      const tone = ctx2.createConstantSource();
      tone.offset.value = 0.5;
      tone.connect(panner);
      panner.connect(ctx2.destination);
      tone.start();
      /* 0.5 / 8 with the equal-power centre: 0.0625 * cos(pi/4) = 0.0442. */
      const front = ctx2.__render(256);
      console.log("front=" + front[0].toFixed(4) + ":" + front[1].toFixed(4));
      panner.setPosition(8, 0, 0);
      const right = ctx2.__render(256);
      console.log("right=" + right[0].toFixed(4) + ":" + right[1].toFixed(4));
      panner.setPosition(-8, 0, 0);
      const left = ctx2.__render(256);
      console.log("left=" + left[0].toFixed(4) + ":" + left[1].toFixed(4));
      /* Moving the listener next to the source doubles the distance gain. */
      ctx2.listener.positionX.value = 8;
      const near = ctx2.__render(256);
      console.log("listener=" + ctx2.listener.positionX.value + ":" + near[0].toFixed(4));
      console.log("panner-api=" + panner.panningModel + ":" + panner.refDistance + ":" +
        panner.coneInnerAngle + ":" + (typeof panner.setOrientation));
      `,
      { console: true, autocloseMs: 6000 },
    );
    expect(value("fft")).toBe("1024:512");
    expect(value("peak")).toBe("true:true");
    expect(value("time")).toBe("true");
    expect(value("smoothing")).toBe("0.25");
    expect(value("front")).toBe("0.0442:0.0442");
    expect(value("right")).toBe("0.0000:0.0625");
    expect(value("left")).toBe("0.0625:0.0000");
    expect(value("listener")).toBe("8:0.0313");
    expect(value("panner-api")).toBe("HRTF:1:360:function");
  });

  it("provides silent media sources and a listener with nine params", () => {
    const { value } = harness.compileAndRun(
      "audio-media",
      `
      const ctx = new AudioContext({ __offline: true });
      const element = ctx.createMediaElementSource(null);
      const stream = ctx.createMediaStreamSource(null);
      const gain = ctx.createGain();
      element.connect(gain);
      stream.connect(gain);
      gain.connect(ctx.destination);
      const silence = ctx.__render(256);
      console.log("media=" + silence[0].toFixed(4) + ":" + (typeof element.connect) +
        ":" + (typeof stream.connect));
      const listener = ctx.listener;
      const names = ["positionX", "positionY", "positionZ", "forwardX", "forwardY",
        "forwardZ", "upX", "upY", "upZ"];
      let params = 0;
      for (const name of names) if (listener[name] != null && typeof listener[name].value === "number") params += 1;
      console.log("listener=" + params + "/" + names.length + ":" + listener.forwardZ.value +
        ":" + (typeof listener.setPosition));
      listener.setPosition(1, 2, 3);
      console.log("position=" + listener.positionX.value + ":" + listener.positionY.value +
        ":" + listener.positionZ.value);
      `,
      { console: true, autocloseMs: 6000 },
    );
    expect(value("media")).toBe("0.0000:function:function");
    expect(value("listener")).toBe("9/9:-1:function");
    expect(value("position")).toBe("1:2:3");
  });
});
