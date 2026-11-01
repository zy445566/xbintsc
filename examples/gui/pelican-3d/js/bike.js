// The bicycle, built from vertex-coloured primitives.
//
// The design is a relaxed city bike: two large wheels, a teal frame, a brown
// saddle, a chain, a brass bell and a warm headlight. Everything that does not
// move is baked into `frameGeometry`; each wheel and the crank are their own
// mesh so they can rotate independently.

import { MeshBuilder, cylinder, hexColor } from "./geometry.js";
import { PI, multiply, rotationX, scaling, translation } from "./math.js";

const PALETTE = {
  frame: hexColor("#12d6c0"),
  frameDeep: hexColor("#09a99e"),
  tyre: hexColor("#1a2029"),
  tyreEdge: hexColor("#2a3442"),
  rim: hexColor("#e4f8f6"),
  spoke: hexColor("#c4e4e5"),
  chrome: hexColor("#aec9cf"),
  saddle: hexColor("#8c5430"),
  saddleTop: hexColor("#c07643"),
  grip: hexColor("#262933"),
  chain: hexColor("#73828b"),
  gold: hexColor("#ffd447"),
  reflector: hexColor("#ff7a2f"),
  light: hexColor("#fff7b8"),
};

/** Dimensions of the bike in its own space (both wheels touch y = 0). */
export const BIKE = {
  wheelRadius: 0.72,
  rearHub: [-1.15, 0.72, 0],
  frontHub: [1.2, 0.72, 0],
  bottomBracket: [0, 0.82, 0],
  seatTop: [-0.42, 1.78, 0],
  headTop: [0.76, 1.76, 0],
  headBottom: [0.84, 1.48, 0],
  saddle: [-0.49, 1.91, 0],
  handle: [0.95, 1.94, 0],
  crankRadius: 0.28,
  pedalZ: 0.18,
};

export function createBikeModel() {
  const frame = new MeshBuilder();

  const rear = BIKE.rearHub;
  const front = BIKE.frontHub;
  const bb = BIKE.bottomBracket;
  const seat = BIKE.seatTop;
  const headTop = BIKE.headTop;
  const headBottom = BIKE.headBottom;

  /* Main triangle. */
  frame.addTube(bb, seat, 0.055, PALETTE.frame, 8);
  frame.addTube(bb, headBottom, 0.06, PALETTE.frame, 8);
  frame.addTube(seat, headTop, 0.055, PALETTE.frame, 8);
  frame.addTube(headBottom, headTop, 0.065, PALETTE.frameDeep, 8);

  /* Rear triangle, doubled on both sides of the wheel. */
  for (let side = -1; side <= 1; side += 2) {
    const z = side * 0.075;
    frame.addTube([rear[0], rear[1], z], [seat[0], seat[1], z], 0.025, PALETTE.frameDeep, 6);
    frame.addTube([rear[0], rear[1], z], [bb[0], bb[1], z], 0.03, PALETTE.frame, 6);
  }

  /* Fork. */
  for (let side = -1; side <= 1; side += 2) {
    const z = side * 0.075;
    frame.addTube([headBottom[0], headBottom[1], z], [front[0], front[1], z], 0.026, PALETTE.frameDeep, 6);
  }

  /* Seat post and saddle. */
  frame.addTube(seat, [BIKE.saddle[0], BIKE.saddle[1] - 0.04, 0], 0.035, PALETTE.chrome, 8);
  frame.addEllipsoid([BIKE.saddle[0] - 0.06, 1.93, 0], 0.26, 0.065, 0.15, PALETTE.saddle);
  frame.addEllipsoid([BIKE.saddle[0] + 0.18, 1.915, 0], 0.18, 0.05, 0.11, PALETTE.saddleTop);

  /* Stem, handlebar and grips. */
  frame.addTube(headTop, BIKE.handle, 0.035, PALETTE.chrome, 8);
  frame.addTube(
    [BIKE.handle[0], BIKE.handle[1], -0.38],
    [BIKE.handle[0], BIKE.handle[1], 0.38],
    0.028,
    PALETTE.chrome,
    8,
  );
  frame.addTube(
    [BIKE.handle[0], BIKE.handle[1], 0.22],
    [BIKE.handle[0], BIKE.handle[1], 0.39],
    0.045,
    PALETTE.grip,
    8,
  );
  frame.addTube(
    [BIKE.handle[0], BIKE.handle[1], -0.39],
    [BIKE.handle[0], BIKE.handle[1], -0.22],
    0.045,
    PALETTE.grip,
    8,
  );

  /* Headlight. */
  frame.addEllipsoid([1.0, 1.42, 0], 0.09, 0.11, 0.11, PALETTE.light);
  frame.addEllipsoid([0.97, 1.42, 0], 0.04, 0.06, 0.06, PALETTE.reflector);

  /* Chain: two thin lines from the chain ring to the rear cog. */
  frame.addTube([bb[0], bb[1] + 0.17, 0.11], [rear[0], rear[1] + 0.1, 0.11], 0.012, PALETTE.chain, 5);
  frame.addTube([bb[0], bb[1] - 0.17, 0.11], [rear[0], rear[1] - 0.1, 0.11], 0.012, PALETTE.chain, 5);
  frame.addTorus([rear[0], rear[1], 0.1], 0.105, 0.015, PALETTE.chrome);

  /* Reflectors on the fork and at the back. */
  frame.addBox([0.98, 1.22, 0.14], 0.035, 0.08, 0.1, PALETTE.reflector);
  frame.addBox([-1.22, 0.98, 0], 0.035, 0.08, 0.08, PALETTE.reflector);

  return {
    frame: frame.build(),
    wheel: buildWheelGeometry(),
    crank: buildCrankGeometry(),
    pedal: buildPedalGeometry(),
    bell: buildBellGeometry(),
    dimensions: BIKE,
  };
}

function buildWheelGeometry() {
  const b = new MeshBuilder();
  b.addTorus([0, 0, 0], 0.655, 0.065, PALETTE.tyre);
  b.addTorus([0, 0, 0], 0.578, 0.016, PALETTE.rim);
  b.addTorus([0, 0, 0], 0.543, 0.006, PALETTE.tyreEdge);

  const angleStep = (Math.PI * 2) / 12;
  for (let i = 0; i < 12; i += 1) {
    const angle = i * angleStep;
    const outer = [Math.cos(angle) * 0.565, Math.sin(angle) * 0.565, 0];
    b.addTube([0, 0, 0], outer, 0.0065, PALETTE.spoke, 4);
  }

  /* Hub along Z: the unit cylinder's Y axis is rotated onto Z. */
  b.add(
    cylinder(10),
    multiply(rotationX(PI / 2), scaling(0.085, 0.13, 0.085)),
    PALETTE.chrome,
  );
  b.addBox([0.35, 0, 0.03], 0.09, 0.045, 0.02, PALETTE.reflector);
  return b.build();
}

function buildCrankGeometry() {
  const b = new MeshBuilder();
  const radius = BIKE.crankRadius;
  b.addTorus([0, 0, 0.115], 0.17, 0.023, PALETTE.chrome);
  b.addTorus([0, 0, 0.135], 0.05, 0.012, PALETTE.frameDeep);
  b.addTube([0, 0, 0.14], [radius, 0, 0.14], 0.027, PALETTE.chrome, 6);
  b.addTube([0, 0, -0.14], [-radius, 0, -0.14], 0.027, PALETTE.chrome, 6);
  b.addEllipsoid([radius, 0, 0.16], 0.045, 0.045, 0.035, PALETTE.frameDeep);
  b.addEllipsoid([-radius, 0, -0.16], 0.045, 0.045, 0.035, PALETTE.frameDeep);
  return b.build();
}

function buildPedalGeometry() {
  const b = new MeshBuilder();
  b.addBox([0, 0, 0], 0.21, 0.045, 0.15, PALETTE.grip);
  b.addBox([0.02, 0.028, 0], 0.14, 0.012, 0.1, PALETTE.reflector);
  return b.build();
}

function buildBellGeometry() {
  const b = new MeshBuilder();
  b.addTube([0, 0.02, 0], [0, 0.1, 0], 0.018, PALETTE.chrome, 6);
  b.addEllipsoid([0, 0.14, 0], 0.105, 0.085, 0.105, PALETTE.gold);
  b.addEllipsoid([0, 0.205, 0], 0.025, 0.02, 0.025, PALETTE.frameDeep);
  return b.build();
}
