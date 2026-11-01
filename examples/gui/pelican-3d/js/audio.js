// Tiny procedural sound engine built on the Web Audio API.
//
// There are no audio files: the bell is a stack of sine partials with an
// exponential decay, the wind is a looping noise buffer through a low-pass
// filter, and the jump/land/whoosh effects are short filtered noise bursts plus
// pitch-swept oscillators. The same code runs in a browser and in the native
// gui engine (which implements the same AudioContext graph).

import { clamp } from "./math.js";

export function createAudio() {
  let ctx = null;
  let master = null;
  let windGain = null;
  let noiseBuffer = null;
  let available = false;
  let muted = false;
  let lastState = "unavailable";

  function ensure() {
    if (ctx !== null) return true;
    try {
      const Ctor = window.AudioContext || window.webkitAudioContext;
      if (Ctor === undefined || Ctor === null) return false;
      ctx = new Ctor();
      master = ctx.createGain();
      master.gain.value = muted ? 0 : 0.30;
      master.connect(ctx.destination);

      const seconds = 2;
      noiseBuffer = ctx.createBuffer(1, Math.floor(ctx.sampleRate * seconds), ctx.sampleRate);
      const data = noiseBuffer.getChannelData(0);
      for (let i = 0; i < data.length; i += 1) {
        data[i] = Math.random() * 2 - 1;
      }

      startWind();
      available = true;
      lastState = String(ctx.state);
      return true;
    } catch (error) {
      ctx = null;
      available = false;
      lastState = "error: " + String(error);
      return false;
    }
  }

  function startWind() {
    if (ctx === null || noiseBuffer === null) return;
    const source = ctx.createBufferSource();
    source.buffer = noiseBuffer;
    source.loop = true;

    const filter = ctx.createBiquadFilter();
    filter.type = "lowpass";
    filter.frequency.value = 520;
    filter.Q.value = 0.7;

    windGain = ctx.createGain();
    windGain.gain.value = 0.012;

    source.connect(filter);
    filter.connect(windGain);
    windGain.connect(master);
    source.start(0);
  }

  function resume() {
    if (!ensure()) return;
    if (ctx.state === "suspended" && typeof ctx.resume === "function") {
      ctx.resume();
    }
    lastState = String(ctx.state);
  }

  function setMuted(value) {
    muted = value;
    if (master === null) return;
    const target = muted ? 0 : 0.30;
    const now = ctx.currentTime;
    master.gain.cancelScheduledValues(now);
    master.gain.setValueAtTime(master.gain.value, now);
    master.gain.linearRampToValueAtTime(target, now + 0.12);
  }

  function setSpeed(speed) {
    if (ctx === null || windGain === null) return;
    const now = ctx.currentTime;
    const amount = clamp(0.006 + (speed - 3) * 0.006, 0.004, 0.075);
    windGain.gain.cancelScheduledValues(now);
    windGain.gain.setTargetAtTime(amount, now, 0.12);
  }

  function envelopeGain(value, attack, release) {
    const gain = ctx.createGain();
    const now = ctx.currentTime;
    gain.gain.setValueAtTime(0.0001, now);
    gain.gain.linearRampToValueAtTime(value, now + attack);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + attack + release);
    gain.connect(master);
    return gain;
  }

  function tone(frequency, endFrequency, duration, volume, type) {
    if (ctx === null) return;
    const now = ctx.currentTime;
    const osc = ctx.createOscillator();
    osc.type = type === undefined ? "sine" : type;
    osc.frequency.setValueAtTime(frequency, now);
    osc.frequency.exponentialRampToValueAtTime(Math.max(20, endFrequency), now + duration);
    const gain = envelopeGain(volume, 0.012, duration);
    osc.connect(gain);
    osc.start(now);
    osc.stop(now + duration + 0.18);
  }

  function noise(duration, type, frequency, endFrequency, volume) {
    if (ctx === null || noiseBuffer === null) return;
    const now = ctx.currentTime;
    const source = ctx.createBufferSource();
    source.buffer = noiseBuffer;
    source.loop = true;

    const filter = ctx.createBiquadFilter();
    filter.type = type;
    filter.frequency.setValueAtTime(frequency, now);
    filter.frequency.exponentialRampToValueAtTime(Math.max(30, endFrequency), now + duration);
    filter.Q.value = 1.4;

    const gain = envelopeGain(volume, 0.02, duration);
    source.connect(filter);
    filter.connect(gain);
    source.start(now);
    source.stop(now + duration + 0.05);
  }

  function bell() {
    if (!ensure()) return;
    resume();
    const now = ctx.currentTime;
    const partials = [
      [1568, 0.105, 1.15],
      [2093, 0.070, 0.90],
      [2637, 0.045, 0.70],
      [1046, 0.035, 1.35],
    ];
    for (let i = 0; i < partials.length; i += 1) {
      const osc = ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.setValueAtTime(partials[i][0], now);
      const gain = ctx.createGain();
      gain.gain.setValueAtTime(0.0001, now);
      gain.gain.linearRampToValueAtTime(partials[i][1], now + 0.006);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + partials[i][2]);
      osc.connect(gain);
      gain.connect(master);
      osc.start(now);
      osc.stop(now + partials[i][2] + 0.05);
    }
  }

  function chime() {
    if (!ensure()) return;
    resume();
    tone(1046.5, 1046.5, 0.18, 0.06, "triangle");
    setTimeout(function () {
      tone(1318.5, 1318.5, 0.18, 0.055, "triangle");
    }, 110);
    setTimeout(function () {
      tone(1568.0, 1568.0, 0.26, 0.05, "triangle");
    }, 220);
  }

  function jump() {
    if (!ensure()) return;
    resume();
    tone(220, 620, 0.20, 0.08, "sine");
    noise(0.20, "highpass", 1200, 3600, 0.035);
  }

  function land() {
    if (!ensure()) return;
    resume();
    tone(120, 42, 0.24, 0.10, "sine");
    noise(0.18, "lowpass", 380, 120, 0.055);
  }

  function whoosh(intensity) {
    if (!ensure()) return;
    resume();
    const amount = clamp(intensity, 0.2, 1.0);
    noise(0.34, "bandpass", 420, 2200, 0.045 * amount);
  }

  function chirp() {
    if (!ensure()) return;
    resume();
    tone(1700, 2600, 0.07, 0.018, "sine");
    setTimeout(function () {
      tone(2300, 3000, 0.06, 0.014, "sine");
    }, 90);
  }

  return {
    ensure: ensure,
    resume: resume,
    setMuted: setMuted,
    setSpeed: setSpeed,
    bell: bell,
    chime: chime,
    jump: jump,
    land: land,
    whoosh: whoosh,
    chirp: chirp,
    isAvailable: function () {
      return available;
    },
    state: function () {
      return ctx === null ? lastState : String(ctx.state);
    },
  };
}
