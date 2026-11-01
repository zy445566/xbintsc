// The world around the rider: a warm coastal road at golden hour, rolling
// hills, trees, roadside posts, flowers and low-poly clouds. Scenery slides
// past with the bike; the hills and the sky stay put for depth.

import {
  MeshBuilder,
  discXZ,
  frustum,
  gridXZ,
  hexColor,
  mixColor,
  sphere,
} from "./geometry.js";
import { multiply, scaling, translation } from "./math.js";

const PALETTE = {
  grassA: hexColor("#5cc96b"),
  grassB: hexColor("#80df7e"),
  grassC: hexColor("#3fae58"),
  grassD: hexColor("#a5ea8c"),
  sand: hexColor("#e7c47c"),
  asphaltA: hexColor("#3d4957"),
  asphaltB: hexColor("#4b5869"),
  asphaltC: hexColor("#2d3743"),
  line: hexColor("#fff3d2"),
  dash: hexColor("#ffd23f"),
  trunk: hexColor("#8c5b37"),
  trunkDark: hexColor("#6b4228"),
  leafA: hexColor("#2eae5e"),
  leafB: hexColor("#45ce76"),
  leafC: hexColor("#1e8c50"),
  pineA: hexColor("#1e8250"),
  pineB: hexColor("#2fb069"),
  post: hexColor("#fff7dd"),
  postRed: hexColor("#ff5847"),
  flower: hexColor("#ffd23f"),
  cloud: hexColor("#ffffff"),
  cloudShade: hexColor("#e8f2ff"),
  hillA: hexColor("#59b59a"),
  hillB: hexColor("#3f8f7c"),
  hillC: hexColor("#7bcfac"),
  shadow: hexColor("#1b2228"),
};

const RANGE = 140;

function hash(value) {
  const n = Math.sin(value * 127.1 + 311.7) * 43758.5453;
  return n - Math.floor(n);
}

function instanceArray(items) {
  const data = new Float32Array(items.length * 4);
  for (let i = 0; i < items.length; i += 1) {
    data[i * 4] = items[i][0];
    data[i * 4 + 1] = items[i][1];
    data[i * 4 + 2] = items[i][2];
    data[i * 4 + 3] = items[i][3];
  }
  return data;
}

export function createWorldModel() {
  const ground = gridXZ(240, 90, 60, 24, function (column, row) {
    const n = hash(column * 3.7 + row * 11.3);
    let color = mixColor(PALETTE.grassA, PALETTE.grassB, n);
    if (n > 0.78) color = mixColor(color, PALETTE.grassD, 0.45);
    if (n < 0.18) color = mixColor(color, PALETTE.grassC, 0.55);
    if (row > 18) color = mixColor(color, PALETTE.grassC, 0.2);
    return color;
  });

  const road = gridXZ(240, 9.2, 60, 8, function (column, row) {
    const n = hash(column * 9.1 + row * 5.3);
    let color = mixColor(PALETTE.asphaltA, PALETTE.asphaltB, n);
    if (n < 0.2) color = mixColor(color, PALETTE.asphaltC, 0.6);
    return color;
  });

  const shoulders = new MeshBuilder();
  shoulders.addBox([0, -0.015, 5.25], 240, 0.05, 1.2, PALETTE.sand);
  shoulders.addBox([0, -0.015, -5.25], 240, 0.05, 1.2, PALETTE.sand);

  const lines = new MeshBuilder();
  lines.addBox([0, 0.026, 4.1], 240, 0.022, 0.13, PALETTE.line);
  lines.addBox([0, 0.026, -4.1], 240, 0.022, 0.13, PALETTE.line);

  const dashes = new MeshBuilder();
  for (let x = -120; x <= 120; x += 8) {
    dashes.addBox([x, 0.035, 0], 2.7, 0.02, 0.17, PALETTE.dash);
  }

  const hills = new MeshBuilder();
  hills.addEllipsoid([-38, -6, -52], 30, 13, 16, PALETTE.hillB);
  hills.addEllipsoid([4, -7, -55], 34, 15, 18, PALETTE.hillA);
  hills.addEllipsoid([46, -5, -51], 27, 11, 15, PALETTE.hillC);
  hills.addEllipsoid([-76, -4, -48], 24, 9, 13, PALETTE.hillC);
  hills.addEllipsoid([78, -4, -46], 22, 8, 12, PALETTE.hillB);

  return {
    ground: ground,
    road: road,
    shoulders: shoulders.build(),
    lines: lines.build(),
    dashes: dashes.build(),
    hills: hills.build(),
    treeRound: {
      geometry: buildRoundTree(),
      instances: instanceArray(roundTreeInstances()),
    },
    treePine: {
      geometry: buildPineTree(),
      instances: instanceArray(pineTreeInstances()),
    },
    flowers: {
      geometry: buildFlowers(),
      instances: instanceArray(flowerInstances()),
    },
    posts: {
      geometry: buildPost(),
      instances: instanceArray(postInstances()),
    },
    clouds: {
      geometry: buildCloud(),
      instances: instanceArray(cloudInstances()),
    },
    shadow: discXZ(36),
  };
}

function buildRoundTree() {
  const b = new MeshBuilder();
  b.addTube([0, 0, 0], [0, 1.45, 0], 0.12, PALETTE.trunk, 7);
  b.addTube([0.02, 0.8, 0], [0.38, 1.45, 0], 0.06, PALETTE.trunkDark, 6);
  b.addEllipsoid([0, 2.05, 0], 0.82, 0.70, 0.78, PALETTE.leafA);
  b.addEllipsoid([-0.42, 1.88, 0.18], 0.55, 0.46, 0.52, PALETTE.leafB);
  b.addEllipsoid([0.44, 2.35, -0.18], 0.48, 0.42, 0.45, PALETTE.leafC);
  b.addEllipsoid([0.10, 2.65, 0.12], 0.44, 0.38, 0.42, PALETTE.leafB);
  return b.build();
}

function buildPineTree() {
  const b = new MeshBuilder();
  b.addTube([0, 0, 0], [0, 1.35, 0], 0.11, PALETTE.trunkDark, 7);
  const layers = [
    [1.85, 1.45, 1.05, PALETTE.pineB],
    [2.55, 1.18, 0.86, PALETTE.pineA],
    [3.18, 0.90, 0.66, PALETTE.pineB],
  ];
  for (let i = 0; i < layers.length; i += 1) {
    const layer = layers[i];
    const matrix = multiply(
      translation(0, layer[0], 0),
      scaling(layer[2], layer[1], layer[2]),
    );
    b.add(frustum(0, 1, 9), matrix, layer[3]);
  }
  return b.build();
}

function buildFlowers() {
  const b = new MeshBuilder();
  const blades = [
    [0, 0, 0, 0.24],
    [0.10, 0, 0.05, 0.18],
    [-0.09, 0, 0.03, 0.20],
  ];
  for (let i = 0; i < blades.length; i += 1) {
    const blade = blades[i];
    b.add(
      frustum(0, 1, 4),
      multiply(
        translation(blade[0], blade[3] * 0.5, blade[2]),
        scaling(0.035, blade[3], 0.035),
      ),
      i === 1 ? PALETTE.grassC : PALETTE.grassB,
    );
  }
  b.addEllipsoid([0.07, 0.29, 0.04], 0.045, 0.045, 0.045, PALETTE.flower);
  return b.build();
}

function buildPost() {
  const b = new MeshBuilder();
  b.addBox([0, 0.46, 0], 0.075, 0.92, 0.075, PALETTE.post);
  b.addBox([0, 0.78, 0.045], 0.095, 0.14, 0.035, PALETTE.postRed);
  return b.build();
}

function buildCloud() {
  const b = new MeshBuilder();
  const puffs = [
    [0, 0, 0, 2.4, 1.1, 1.3, PALETTE.cloud],
    [-1.8, -0.25, 0.2, 1.5, 0.8, 1.0, PALETTE.cloudShade],
    [1.8, -0.20, -0.15, 1.6, 0.82, 0.95, PALETTE.cloud],
    [0.35, 0.6, 0.1, 1.2, 0.72, 0.8, PALETTE.cloud],
    [-0.8, 0.34, -0.2, 1.05, 0.6, 0.72, PALETTE.cloudShade],
  ];
  for (let i = 0; i < puffs.length; i += 1) {
    const p = puffs[i];
    b.add(
      sphere(16, 24),
      multiply(
        translation(p[0], p[1], p[2]),
        scaling(p[3], p[4], p[5]),
      ),
      p[6],
    );
  }
  return b.build();
}

function roundTreeInstances() {
  const items = [];
  for (let i = 0; i < 15; i += 1) {
    const n = hash(i * 7.1);
    const x = (i / 15) * RANGE + n * 4.0;
    const z = -9.5 - hash(i * 3.3) * 4.0;
    const scale = 0.85 + hash(i * 9.7) * 0.6;
    items.push([x, z, scale, 0]);
  }
  return items;
}

function pineTreeInstances() {
  const items = [];
  for (let i = 0; i < 18; i += 1) {
    const n = hash(i * 5.4);
    const x = (i / 18) * (RANGE + 20) + n * 5.0;
    const z = -16.5 - hash(i * 2.7) * 5.0;
    const scale = 1.1 + hash(i * 8.2) * 0.8;
    items.push([x, z, scale, 0]);
  }
  return items;
}

function flowerInstances() {
  const items = [];
  for (let i = 0; i < 44; i += 1) {
    const n = hash(i * 2.9);
    const x = (i / 44) * RANGE + n * 2.8;
    const z = 5.0 + hash(i * 6.7) * 2.4;
    const scale = 0.7 + hash(i * 1.3) * 0.8;
    items.push([x, z, scale, 0]);
  }
  for (let i = 0; i < 36; i += 1) {
    const n = hash(i * 4.1 + 4);
    const x = (i / 36) * RANGE + n * 3.2;
    const z = -5.2 - hash(i * 8.9) * 2.2;
    const scale = 0.65 + hash(i * 3.4) * 0.7;
    items.push([x, z, scale, 0]);
  }
  return items;
}

function postInstances() {
  const items = [];
  for (let i = 0; i < 16; i += 1) {
    const x = (i / 16) * RANGE;
    items.push([x, -5.2, 1, 0]);
  }
  return items;
}

function cloudInstances() {
  const items = [];
  for (let i = 0; i < 9; i += 1) {
    const x = (i / 9) * 200;
    const z = -47 - hash(i * 4.2) * 8;
    const scale = 0.65 + hash(i * 7.8) * 0.55;
    const y = 7.2 + hash(i * 2.2) * 2.8;
    items.push([x, z, scale, y]);
  }
  return items;
}
