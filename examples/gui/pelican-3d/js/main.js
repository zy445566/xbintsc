// Pelican Ride 3D — the entry module.
//
// This file wires the independently-built pieces together: the bike model, the
// pelican model, the world, a renderer and the Web Audio sound engine.
// `index.html` loads it as a real ES module, so a browser and the xbintsc gui
// extension execute the same module graph (the compiler defers the graph until
// the document has been parsed).
//
// Two GPU backends share the scene: WebGL 2 (renderer-webgl.js) and WebGPU
// (renderer-webgpu.js). They live on separate `<canvas>` elements — a canvas
// can only ever have one context kind — and the HUD button (or the G key)
// switches between them at run time.

import { createAudio } from "./audio.js";
import { BIKE, createBikeModel } from "./bike.js";
import { PELICAN, createPelicanModel } from "./pelican.js";
import { createWorldModel } from "./world.js";
import { createRenderer, drawTube } from "./renderer.js";
import {
  clamp,
  lookAt,
  multiply,
  perspective,
  pivot,
  rotationZ,
  scaling,
  transformPoint,
  translation,
} from "./math.js";
import { hexColor } from "./geometry.js";

const WIDTH = 1100;
const HEIGHT = 700;
const FOG = [1.0, 0.77, 0.50];
const LEG_NEAR = hexColor("#ffb02e");
const LEG_FAR = hexColor("#e57a0c");
const SCARF = hexColor("#e22c44");
const WHITE = [1, 1, 1];

/** Change this to start in the other backend; `?backend=webgpu` also works in
 * a browser, and `window.__pelicanBackend` overrides both. */
const DEFAULT_BACKEND = "webgl2";

const canvasWebgl = document.getElementById("scene-webgl2");
const canvasGpu = document.getElementById("scene-webgpu");

const hooks = {
  ok: false,
  frames: 0,
  error: "",
  size: WIDTH + "x" + HEIGHT,
  backend: "",
  info: function () {
    return { frames: 0 };
  },
  probe: function () {
    return "0,0,0,0";
  },
  probeMax: function () {
    return "0,0,0,0";
  },
  snapshot: function () {
    return new Uint8Array(0);
  },
  setBackend: function () {
    return false;
  },
  ringBell: function () {},
  jump: function () {},
  toggleSound: function () {},
  reset: function () {},
};
window.__pelicanRide3d = hooks;

function showError(message) {
  const box = document.getElementById("error");
  if (box !== null && box !== undefined) {
    box.textContent = message;
    box.style.display = "block";
  }
}

function hideError() {
  const box = document.getElementById("error");
  if (box !== null && box !== undefined) box.style.display = "none";
}

/* --------------------------------------------------------------- the scene */

let models = null;
const scenes = { webgl2: null, webgpu: null };
let activeBackend = "";
let activeScene = null;
let switching = false;
let webgpuAvailable = false;

function ensureModels() {
  if (models === null) {
    models = {
      world: createWorldModel(),
      bike: createBikeModel(),
      pelican: createPelicanModel(),
    };
  }
  return models;
}

function createMeshes(renderer, built) {
  const worldModel = built.world;
  const bikeModel = built.bike;
  const pelicanModel = built.pelican;
  return {
    ground: renderer.createMesh(worldModel.ground),
    road: renderer.createMesh(worldModel.road),
    shoulders: renderer.createMesh(worldModel.shoulders),
    lines: renderer.createMesh(worldModel.lines),
    dashes: renderer.createMesh(worldModel.dashes),
    hills: renderer.createMesh(worldModel.hills),
    treeRound: renderer.createInstancedMesh(worldModel.treeRound.geometry, worldModel.treeRound.instances),
    treePine: renderer.createInstancedMesh(worldModel.treePine.geometry, worldModel.treePine.instances),
    flowers: renderer.createInstancedMesh(worldModel.flowers.geometry, worldModel.flowers.instances),
    posts: renderer.createInstancedMesh(worldModel.posts.geometry, worldModel.posts.instances),
    clouds: renderer.createInstancedMesh(worldModel.clouds.geometry, worldModel.clouds.instances),
    shadow: renderer.createMesh(worldModel.shadow),
    frame: renderer.createMesh(bikeModel.frame),
    wheel: renderer.createMesh(bikeModel.wheel),
    crank: renderer.createMesh(bikeModel.crank),
    pedal: renderer.createMesh(bikeModel.pedal),
    bell: renderer.createMesh(bikeModel.bell),
    pelican: renderer.createMesh(pelicanModel.body),
    wingNear: renderer.createMesh(pelicanModel.wingNear),
    wingFar: renderer.createMesh(pelicanModel.wingFar),
  };
}

function canvasFor(backend) {
  return backend === "webgpu" ? canvasGpu : canvasWebgl;
}

async function ensureScene(backend) {
  const existing = scenes[backend];
  if (existing !== null) return existing;
  const built = ensureModels();
  const renderer = await createRenderer(backend, canvasFor(backend), WIDTH, HEIGHT);
  const meshes = createMeshes(renderer, built);
  const scene = { renderer: renderer, meshes: meshes };
  scenes[backend] = scene;
  return scene;
}

function activateScene(backend, scene) {
  activeBackend = backend;
  activeScene = scene;
  canvasWebgl.style.display = backend === "webgl2" ? "block" : "none";
  canvasGpu.style.display = backend === "webgpu" ? "block" : "none";
  hooks.ok = true;
  hooks.error = "";
  hooks.glVersion = scene.renderer.glVersion;
  hooks.glslVersion = scene.renderer.glslVersion;
  updateHud();
}

async function useBackend(backend) {
  if (switching) return false;
  if (backend === "webgpu" && !webgpuAvailable) {
    showError("WebGPU is not available on this host (no navigator.gpu, or the gui webgpu link feature is off).");
    return false;
  }
  switching = true;
  setBackendLabel("…");
  try {
    const scene = await ensureScene(backend);
    activateScene(backend, scene);
    hideError();
    switching = false;
    return true;
  } catch (error) {
    switching = false;
    hooks.error = String(error);
    showError("Render backend " + backend + " failed: " + String(error));
    updateHud();
    return false;
  }
}

function toggleBackend() {
  if (switching) return;
  if (activeBackend === "webgl2") {
    useBackend("webgpu");
  } else {
    useBackend("webgl2");
  }
}

function preferredBackend() {
  let forced = window.__pelicanBackend;
  if (forced === "webgpu" || forced === "webgl2") return forced;
  let search = "";
  const locationObject = window.location;
  if (locationObject !== null && locationObject !== undefined) {
    search = String(locationObject.search);
  }
  if (search.indexOf("backend=webgpu") >= 0) return "webgpu";
  if (search.indexOf("backend=webgl2") >= 0) return "webgl2";
  return DEFAULT_BACKEND;
}

function detectWebgpu() {
  if (canvasGpu === null || canvasGpu === undefined) return;
  const gpu = navigator.gpu;
  if (gpu === null || gpu === undefined) return;
  const context = canvasGpu.getContext("webgpu");
  webgpuAvailable = context !== null && context !== undefined;
}

/* ----------------------------------------------------------------- runtime */

const audio = createAudio();
audio.ensure();

const state = {
  time: 0,
  frames: 0,
  speed: 5.8,
  distance: 0,
  scroll: 0,
  jumpY: 0,
  jumpV: 0,
  airborne: false,
  wheelAngle: 0,
  crankAngle: 0,
  bob: 0,
  lean: 0,
  bodyBob: 0,
  bodyLean: 0,
  wingAngle: 0,
  bellPulse: 0,
  boost: false,
  brake: false,
  paused: false,
  soundOn: true,
  cameraYaw: 0.24,
  cameraPitch: 0.19,
  cameraDistance: 11.0,
  dragging: false,
  lastPointerX: 0,
  lastPointerY: 0,
  nextChime: 100,
  nextChirp: 9,
  fps: 60,
};

const speedEl = document.getElementById("speed");
const distanceEl = document.getElementById("distance");
const modeEl = document.getElementById("mode");
const fpsEl = document.getElementById("fps");
const soundEl = document.getElementById("sound");
const soundLabelEl = document.getElementById("sound-label");
const backendEl = document.getElementById("backend");
const backendLabelEl = document.getElementById("backend-label");
const statusEl = document.getElementById("status");

function setText(element, text) {
  if (element !== null && element !== undefined) element.textContent = text;
}

function setBackendLabel(text) {
  setText(backendLabelEl, text);
}

function modeName() {
  if (state.paused) return "PAUSED";
  if (state.airborne) return "AIRBORNE";
  if (state.boost) return "BOOST";
  if (state.brake) return "BRAKE";
  return "CRUISE";
}

function updateHud() {
  const kmh = Math.round(state.speed * 3.6);
  let speedText = String(kmh);
  while (speedText.length < 3) speedText = "0" + speedText;
  setText(speedEl, speedText);
  setText(distanceEl, Math.floor(state.distance) + " m");
  setText(modeEl, modeName());
  setText(fpsEl, String(Math.round(state.fps)));

  const renderer = activeScene === null ? null : activeScene.renderer;
  const backendText = renderer === null ? "…" : renderer.label;
  setBackendLabel(backendText);
  setText(statusEl, renderer === null ? "STARTING" : backendText + " · " + renderer.gpuLabel);

  const audioRunning = audio.state() === "running";
  const soundText = !audio.isAvailable()
    ? "SOUND N/A"
    : !state.soundOn
      ? "SOUND OFF"
      : audioRunning
        ? "SOUND ON"
        : "SOUND: CLICK";
  setText(soundLabelEl, soundText);
}

function jump() {
  if (state.paused || state.airborne) return;
  state.airborne = true;
  state.jumpV = 6.25;
  audio.jump();
  audio.whoosh(0.5);
}

function ringBell() {
  state.bellPulse = 1;
  audio.bell();
}

function toggleSound() {
  state.soundOn = !state.soundOn;
  audio.setMuted(!state.soundOn);
  updateHud();
}

function resetRide() {
  state.speed = 5.8;
  state.distance = 0;
  state.scroll = 0;
  state.jumpY = 0;
  state.jumpV = 0;
  state.airborne = false;
  state.nextChime = 100;
  updateHud();
}

function togglePause() {
  state.paused = !state.paused;
  updateHud();
}

function onKeyDown(event) {
  audio.resume();
  const code = event.code;
  if (code === "Space" || code === "ArrowUp") {
    if (event.preventDefault) event.preventDefault();
    if (!event.repeat) jump();
    return;
  }
  if (event.repeat) return;
  if (code === "KeyB") ringBell();
  else if (code === "KeyM") toggleSound();
  else if (code === "KeyR") resetRide();
  else if (code === "KeyP") togglePause();
  else if (code === "KeyG") toggleBackend();
  else if (code === "KeyW" || code === "ArrowRight") state.boost = true;
  else if (code === "KeyS" || code === "ArrowLeft") state.brake = true;
}

function onKeyUp(event) {
  const code = event.code;
  if (code === "KeyW" || code === "ArrowRight") state.boost = false;
  if (code === "KeyS" || code === "ArrowLeft") state.brake = false;
}

function onMouseDown(event) {
  audio.resume();
  if (event.button !== undefined && event.button !== 0 && event.button !== -1) return;
  state.dragging = true;
  state.lastPointerX = event.clientX === undefined ? 0 : event.clientX;
  state.lastPointerY = event.clientY === undefined ? 0 : event.clientY;
}

function onMouseMove(event) {
  if (!state.dragging) return;
  const x = event.clientX === undefined ? 0 : event.clientX;
  const y = event.clientY === undefined ? 0 : event.clientY;
  const dx = x - state.lastPointerX;
  const dy = y - state.lastPointerY;
  state.lastPointerX = x;
  state.lastPointerY = y;
  state.cameraYaw -= dx * 0.006;
  state.cameraPitch = clamp(state.cameraPitch + dy * 0.004, 0.04, 0.85);
}

function onMouseUp() {
  state.dragging = false;
}

function onWheel(event) {
  const delta = event.deltaY === undefined ? 0 : event.deltaY;
  state.cameraDistance = clamp(state.cameraDistance + delta * 0.004, 6.2, 15);
}

window.addEventListener("keydown", onKeyDown);
window.addEventListener("keyup", onKeyUp);
window.addEventListener("mousedown", onMouseDown);
window.addEventListener("mousemove", onMouseMove);
window.addEventListener("mouseup", onMouseUp);
window.addEventListener("wheel", onWheel);
if (soundEl !== null && soundEl !== undefined) {
  soundEl.addEventListener("click", function (event) {
    if (event.stopPropagation) event.stopPropagation();
    if (event.preventDefault) event.preventDefault();
    audio.resume();
    /* The first click in a browser also unlocks the context; in that case
       turn sound on instead of interpreting it as a mute toggle. */
    if (audio.state() !== "running") {
      state.soundOn = true;
      audio.setMuted(false);
      updateHud();
      return;
    }
    toggleSound();
  });
}
if (backendEl !== null && backendEl !== undefined) {
  backendEl.addEventListener("click", function (event) {
    if (event.stopPropagation) event.stopPropagation();
    if (event.preventDefault) event.preventDefault();
    toggleBackend();
  });
}

/* --------------------------------------------------------------- animation */

function simulate(dt) {
  let target = 5.8;
  if (state.boost) target = 10.6;
  if (state.brake) target = 3.1;
  state.speed += (target - state.speed) * Math.min(1, dt * 2.8);

  const travel = state.speed * dt;
  state.scroll += travel;
  state.distance += travel;

  state.wheelAngle -= travel / BIKE.wheelRadius;
  state.crankAngle -= (travel / BIKE.wheelRadius) * 0.38;

  if (state.airborne) {
    state.jumpV -= 19 * dt;
    state.jumpY += state.jumpV * dt;
    if (state.jumpY <= 0) {
      state.jumpY = 0;
      state.jumpV = 0;
      state.airborne = false;
      audio.land();
    }
  }

  state.bob = Math.sin(state.time * 5.4) * 0.018 + Math.sin(state.time * 11.2) * 0.005;
  state.lean = Math.sin(state.time * 1.7) * 0.006;
  state.bodyBob = Math.sin(state.time * 5.4 + 0.7) * 0.014;
  state.bodyLean = Math.sin(state.time * 1.2) * 0.01;
  state.wingAngle = 0.06 + Math.sin(state.time * 2.2) * 0.035 + (state.airborne ? 0.30 : 0);
  state.wingAngle += state.bellPulse * 0.25;
  state.bellPulse = Math.max(0, state.bellPulse - dt * 2.4);

  if (state.distance >= state.nextChime) {
    state.nextChime += 100;
    audio.chime();
  }
  if (state.time >= state.nextChirp) {
    state.nextChirp = state.time + 8 + Math.random() * 8;
    audio.chirp();
  }
  if ((state.frames & 15) === 0) audio.setSpeed(state.speed);
}

function render() {
  if (activeScene === null) return;
  const renderer = activeScene.renderer;
  const meshes = activeScene.meshes;

  const target = [0.18, 1.95 + state.jumpY * 0.35 + state.bob * 2, -0.1];
  const yaw = state.cameraYaw + Math.sin(state.time * 0.18) * 0.018;
  const pitch = clamp(state.cameraPitch + Math.sin(state.time * 0.11) * 0.008, 0.04, 0.85);
  const cp = Math.cos(pitch);
  const eye = [
    target[0] + Math.sin(yaw) * cp * state.cameraDistance,
    target[1] + Math.sin(pitch) * state.cameraDistance,
    target[2] + Math.cos(yaw) * cp * state.cameraDistance,
  ];
  const projection = perspective(0.72, WIDTH / HEIGHT, 0.1, 190);
  const view = lookAt(eye, target, [0, 1, 0]);
  const viewProj = multiply(projection, view);
  const lightDir = [-0.45, 0.82, 0.36];

  renderer.begin(WIDTH, HEIGHT, viewProj, eye, lightDir, FOG, 0.0068);
  renderer.drawSky(WIDTH, HEIGHT, state.time);

  const identityModel = new Float32Array(16);
  identityModel[0] = 1;
  identityModel[5] = 1;
  identityModel[10] = 1;
  identityModel[15] = 1;

  renderer.draw(meshes.hills, identityModel, [1, 1, 1], WHITE, 1);
  renderer.drawInstanced(meshes.clouds, state.scroll * 0.035, 200);
  /* Keep the ground below the road: both are flat grids, and two coplanar
     layers would z-fight in stripes on the tarmac. */
  renderer.draw(meshes.ground, translation(0, -0.06, 0), [1, 1, 1], WHITE, 1);
  renderer.draw(meshes.road, translation(0, 0.006, 0), [1, 1, 1], WHITE, 1);
  renderer.draw(meshes.shoulders, identityModel, [1, 1, 1], WHITE, 1);
  renderer.draw(meshes.lines, identityModel, [1, 1, 1], WHITE, 1);

  const dash = translation(-(state.scroll % 8), 0, 0);
  renderer.draw(meshes.dashes, dash, [1, 1, 1], WHITE, 1);

  renderer.drawInstanced(meshes.treePine, state.scroll * 0.82, 160);
  renderer.drawInstanced(meshes.treeRound, state.scroll, 140);
  renderer.drawInstanced(meshes.posts, state.scroll, 140);
  renderer.drawInstanced(meshes.flowers, state.scroll, 140);

  const shadowScale = clamp(1 - state.jumpY * 0.23, 0.55, 1);
  const shadowModel = multiply(
    translation(0, 0.052, 0),
    scaling(1.62 * shadowScale, 1, 0.62 * shadowScale),
  );
  renderer.drawShadow(meshes.shadow, shadowModel, 0.34 * shadowScale);

  const root = multiply(
    translation(0, state.jumpY + state.bob, 0),
    rotationZ(state.lean),
  );
  const body = multiply(
    multiply(root, translation(0, state.bodyBob, 0)),
    pivot(
      PELICAN.bodyPivot[0],
      PELICAN.bodyPivot[1],
      PELICAN.bodyPivot[2],
      rotationZ(state.bodyLean),
    ),
  );

  /* Bicycle. */
  renderer.draw(meshes.frame, root, [1, 1, 1], WHITE, 1);
  const rearWheel = multiply(
    root,
    multiply(
      translation(BIKE.rearHub[0], BIKE.rearHub[1], BIKE.rearHub[2]),
      rotationZ(state.wheelAngle),
    ),
  );
  const frontWheel = multiply(
    root,
    multiply(
      translation(BIKE.frontHub[0], BIKE.frontHub[1], BIKE.frontHub[2]),
      rotationZ(state.wheelAngle + 0.08),
    ),
  );
  renderer.draw(meshes.wheel, rearWheel, [1, 1, 1], WHITE, 1);
  renderer.draw(meshes.wheel, frontWheel, [1, 1, 1], WHITE, 1);

  const crank = multiply(
    root,
    multiply(
      translation(BIKE.bottomBracket[0], BIKE.bottomBracket[1], BIKE.bottomBracket[2]),
      rotationZ(state.crankAngle),
    ),
  );
  renderer.draw(meshes.crank, crank, [1, 1, 1], WHITE, 1);

  const crankRadius = BIKE.crankRadius;
  const bb = BIKE.bottomBracket;
  const pedalNear = [
    bb[0] + Math.cos(state.crankAngle) * crankRadius,
    bb[1] + Math.sin(state.crankAngle) * crankRadius,
    BIKE.pedalZ,
  ];
  const pedalFar = [
    bb[0] - Math.cos(state.crankAngle) * crankRadius,
    bb[1] - Math.sin(state.crankAngle) * crankRadius,
    -BIKE.pedalZ,
  ];
  renderer.draw(
    meshes.pedal,
    multiply(root, translation(pedalNear[0], pedalNear[1], pedalNear[2])),
    [1, 1, 1],
    WHITE,
    1,
  );
  renderer.draw(
    meshes.pedal,
    multiply(root, translation(pedalFar[0], pedalFar[1], pedalFar[2])),
    [1, 1, 1],
    WHITE,
    1,
  );

  /* Legs: a two-bone IK reaches from the hip to the moving pedal. */
  const legs = [
    { hip: PELICAN.hips[0], foot: pedalNear },
    { hip: PELICAN.hips[1], foot: pedalFar },
  ];
  for (let i = 0; i < legs.length; i += 1) {
    drawLeg(legs[i].hip, legs[i].foot, body, root, renderer);
  }

  /* Pelican. */
  renderer.draw(meshes.pelican, body, [1, 1, 1], WHITE, 1);
  const shoulders = [PELICAN.shoulders[0], PELICAN.shoulders[1]];
  const wings = [meshes.wingNear, meshes.wingFar];
  for (let i = 0; i < wings.length; i += 1) {
    const shoulder = shoulders[i];
    const wingModel = multiply(
      body,
      multiply(
        translation(shoulder[0], shoulder[1], shoulder[2]),
        rotationZ(state.wingAngle),
      ),
    );
    renderer.draw(wings[i], wingModel, [1, 1, 1], WHITE, 1);
  }

  /* Scarf streamers. */
  drawScarf(body, renderer);

  /* Bell, with a tiny scale pulse when it rings. */
  const bellScale = 1 + state.bellPulse * 0.28;
  const bellModel = multiply(
    root,
    multiply(
      translation(BIKE.handle[0], BIKE.handle[1] + 0.07, 0.23),
      scaling(bellScale, bellScale, bellScale),
    ),
  );
  renderer.draw(meshes.bell, bellModel, [1, 1, 1], WHITE, 1);

  renderer.end();

  hooks.frames = state.frames;
  hooks.draws = renderer.stats.draws;
  hooks.triangles = renderer.stats.triangles;
}

function drawLeg(hipLocal, footLocal, bodyTransform, root, renderer) {
  const hip = transformPoint(bodyTransform, hipLocal[0], hipLocal[1], hipLocal[2]);
  const foot = transformPoint(root, footLocal[0], footLocal[1], footLocal[2]);
  foot[1] += 0.045;
  foot[2] += footLocal[2] > 0 ? 0.045 : -0.045;

  const dx = foot[0] - hip[0];
  const dy = foot[1] - hip[1];
  const dz = foot[2] - hip[2];
  let d = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (d < 0.001) d = 0.001;
  const ux = dx / d;
  const uy = dy / d;
  const uz = dz / d;

  const l1 = PELICAN.thigh;
  const l2 = PELICAN.shin;
  const reach = Math.min(d, l1 + l2 - 0.002);
  const a = (l1 * l1 - l2 * l2 + reach * reach) / (2 * reach);
  const h = Math.sqrt(Math.max(l1 * l1 - a * a, 0.0001));

  let px = -uy;
  let py = ux;
  let pz = 0;
  let pl = Math.sqrt(px * px + py * py);
  if (pl < 0.001) {
    px = 0;
    py = 0;
    pz = 1;
    pl = 1;
  }
  px /= pl;
  py /= pl;
  if (px < 0) {
    px = -px;
    py = -py;
  }

  const knee = [
    hip[0] + ux * a + px * h,
    hip[1] + uy * a + py * h,
    hip[2] + uz * a + pz * h,
  ];
  const leg = footLocal[2] > 0 ? LEG_NEAR : LEG_FAR;
  drawTube(renderer, renderer.unitCylinder, hip, knee, 0.082, leg);
  drawTube(renderer, renderer.unitCylinder, knee, foot, 0.068, leg);
  renderer.draw(
    renderer.unitSphere,
    multiply(translation(knee[0], knee[1], knee[2]), scaling(0.09, 0.09, 0.09)),
    [1, 1, 1],
    leg,
    1,
  );
  renderer.draw(
    renderer.unitSphere,
    multiply(
      translation(foot[0], foot[1], foot[2]),
      scaling(0.18, 0.052, 0.11),
    ),
    [1 / 0.18, 1 / 0.052, 1 / 0.11],
    leg,
    1,
  );
  for (let toe = -1; toe <= 1; toe += 1) {
    renderer.draw(
      renderer.unitBox,
      multiply(
        translation(foot[0] + 0.09, foot[1] + 0.004, foot[2] + toe * 0.043),
        scaling(0.12, 0.024, 0.032),
      ),
      [1 / 0.12, 1 / 0.024, 1 / 0.032],
      leg,
      1,
    );
  }
}

function drawScarf(bodyTransform, renderer) {
  for (let side = -1; side <= 1; side += 2) {
    let previous = transformPoint(bodyTransform, 0.58, 3.53, side * 0.16);
    for (let i = 1; i <= 3; i += 1) {
      const x = previous[0] - 0.15;
      const y = 3.53 + i * 0.055 + Math.sin(state.time * 7.5 - i * 0.9 + side) * 0.035;
      const z = previous[2] + side * 0.022 + Math.sin(state.time * 5.2 - i * 0.7 + side * 1.7) * 0.055;
      const point = [x, y, z];
      drawTube(renderer, renderer.unitCylinder, previous, point, 0.034, SCARF);
      previous = point;
    }
    renderer.draw(
      renderer.unitSphere,
      multiply(
        translation(previous[0], previous[1], previous[2]),
        scaling(0.075, 0.05, 0.07),
      ),
      [1 / 0.075, 1 / 0.05, 1 / 0.07],
      SCARF,
      1,
    );
  }
}

/* ------------------------------------------------------------------- report */

function info() {
  const renderer = activeScene === null ? null : activeScene.renderer;
  return {
    ok: renderer !== null,
    backend: activeBackend,
    renderer: renderer === null ? "none" : renderer.label,
    frames: state.frames,
    fps: state.fps,
    speed: state.speed,
    distance: state.distance,
    draws: renderer === null ? 0 : renderer.stats.draws,
    triangles: renderer === null ? 0 : Math.round(renderer.stats.triangles),
    airborne: state.airborne,
    sound: audio.isAvailable() ? (state.soundOn ? "on" : "off") : "unavailable",
    audioState: audio.state(),
    mode: modeName(),
    size: WIDTH + "x" + HEIGHT,
    glVersion: hooks.glVersion,
    glslVersion: hooks.glslVersion,
  };
}

hooks.info = info;
hooks.jump = jump;
hooks.ringBell = ringBell;
hooks.toggleSound = toggleSound;
hooks.reset = resetRide;
hooks.setBackend = function (backend) {
  return useBackend(backend);
};
hooks.probe = function (x, y) {
  return activeScene === null ? "0,0,0,0" : activeScene.renderer.probe(x, y);
};
hooks.snapshot = function () {
  return activeScene === null ? new Uint8Array(0) : activeScene.renderer.snapshot();
};
hooks.probeMax = function (x, y, width, height) {
  return activeScene === null ? "0,0,0,0" : activeScene.renderer.probeMax(x, y, width, height);
};

/* --------------------------------------------------------------- life cycle */

async function boot() {
  if (canvasWebgl === null || canvasWebgl === undefined || canvasGpu === null || canvasGpu === undefined) {
    showError("Missing the <canvas> elements (expected #scene-webgl2 and #scene-webgpu)");
    return;
  }
  detectWebgpu();
  const wanted = preferredBackend();
  if (wanted === "webgpu" && webgpuAvailable) {
    if (await useBackend("webgpu")) return;
  }
  if (await useBackend("webgl2")) return;
  if (webgpuAvailable) {
    if (await useBackend("webgpu")) return;
  }
  showError("No usable GPU backend: WebGL 2 and WebGPU both failed to start.");
}

updateHud();
boot();

let lastTime = 0;
function frame(now) {
  if (lastTime === 0) lastTime = now;
  let dt = (now - lastTime) / 1000;
  lastTime = now;
  if (dt > 0) {
    state.fps = state.fps * 0.9 + (1 / dt) * 0.1;
  }
  if (!(dt > 0)) dt = 1 / 60;
  if (dt > 0.05) dt = 0.05;

  state.frames += 1;
  if (!state.paused) {
    state.time += dt;
    simulate(dt);
  }
  try {
    render();
  } catch (error) {
    hooks.error = String(error);
    showError("Render failed: " + String(error));
  }
  if (activeScene !== null && (state.frames & 3) === 0) updateHud();
  window.requestAnimationFrame(frame);
}

window.requestAnimationFrame(frame);
console.log("pelican-3d ready with WebGL 2 + WebGPU backends");
