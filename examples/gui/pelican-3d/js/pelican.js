// A stylised low-poly pelican: cream body, long neck, a huge orange bill with
// a coral pouch, a teal helmet and a red scarf. The body is one baked mesh;
// each wing is a second mesh that can flap around its shoulder.

import { MeshBuilder, frustum, hexColor } from "./geometry.js";
import { PI, alignY, multiply, rotationX, rotationZ, scaling, translation } from "./math.js";

const PALETTE = {
  cream: hexColor("#fff4da"),
  creamShade: hexColor("#f3ddb2"),
  creamDeep: hexColor("#e5c28b"),
  white: hexColor("#ffffff"),
  beak: hexColor("#ffa21c"),
  beakLight: hexColor("#ffc93f"),
  beakDeep: hexColor("#e57a0c"),
  pouch: hexColor("#ff7454"),
  pouchLight: hexColor("#ff9f7f"),
  eye: hexColor("#17212c"),
  eyeGlint: hexColor("#ffffff"),
  helmet: hexColor("#12c9c0"),
  helmetDeep: hexColor("#079a95"),
  scarf: hexColor("#ec2f48"),
  scarfDeep: hexColor("#b91f35"),
  gray: hexColor("#c8d2d8"),
};

/** Where the rider's joints are, in bike-local coordinates. */
export const PELICAN = {
  bodyCenter: [0, 2.52, 0],
  bodyPivot: [0, 2.15, 0],
  hips: [
    [-0.22, 1.86, 0.66],
    [-0.22, 1.86, -0.66],
  ],
  shoulders: [
    [0.30, 2.68, 0.68],
    [0.30, 2.68, -0.68],
  ],
  neckBase: [0.34, 3.10, 0.0],
  thigh: 0.86,
  shin: 0.86,
};

export function createPelicanModel() {
  const body = new MeshBuilder();

  /* Torso: an egg sitting on the saddle, with a fuller chest. */
  body.addEllipsoid([0, 2.52, 0], 0.58, 0.62, 0.44, PALETTE.cream, rotationZ(0.12));
  body.addEllipsoid([0.24, 2.61, 0], 0.33, 0.36, 0.40, PALETTE.creamShade, rotationZ(0.10));
  body.addEllipsoid([-0.22, 2.49, 0], 0.28, 0.30, 0.36, PALETTE.creamShade, rotationZ(0.12));
  body.addEllipsoid([0.04, 2.33, 0.17], 0.28, 0.20, 0.13, PALETTE.white);

  /* A folded wing panel breaks up the large cream flank. */
  body.addEllipsoid([-0.10, 2.43, 0.44], 0.34, 0.36, 0.045, PALETTE.creamShade, rotationZ(-0.18));
  body.addEllipsoid([-0.36, 2.32, 0.42], 0.20, 0.20, 0.035, PALETTE.creamDeep, rotationZ(-0.22));

  /* S-curved neck, from the chest up to the head. */
  const neck = [
    [0.34, 2.94, 0],
    [0.44, 3.12, 0],
    [0.56, 3.30, 0],
    [0.68, 3.46, 0],
  ];
  body.addTube(neck[0], neck[1], 0.20, PALETTE.cream, 10);
  body.addTube(neck[1], neck[2], 0.18, PALETTE.cream, 10);
  body.addTube(neck[2], neck[3], 0.16, PALETTE.cream, 10);
  for (let i = 0; i < neck.length; i += 1) {
    const r = 0.21 - i * 0.015;
    body.addEllipsoid(neck[i], r, r * 0.9, r, PALETTE.cream);
  }

  /* Head. */
  body.addEllipsoid([0.80, 3.56, 0], 0.30, 0.28, 0.27, PALETTE.cream);
  body.addEllipsoid([0.73, 3.69, 0], 0.19, 0.14, 0.20, PALETTE.white);

  /* Upper bill: a long tapered wedge. */
  const billBase = [0.99, 3.52, 0];
  const billTip = [1.96, 3.34, 0];
  const billDir = [billTip[0] - billBase[0], billTip[1] - billBase[1], billTip[2] - billBase[2]];
  const billLength = Math.sqrt(
    billDir[0] * billDir[0] + billDir[1] * billDir[1] + billDir[2] * billDir[2],
  );
  const billMid = [
    (billBase[0] + billTip[0]) / 2,
    (billBase[1] + billTip[1]) / 2,
    (billBase[2] + billTip[2]) / 2,
  ];
  const billMatrix = multiply(
    translation(billMid[0], billMid[1], billMid[2]),
    multiply(
      alignY([billDir[0] / billLength, billDir[1] / billLength, billDir[2] / billLength]),
      scaling(1.22, billLength, 1.46),
    ),
  );
  body.add(frustum(0.032, 0.145, 10), billMatrix, PALETTE.beak);
  body.addEllipsoid([1.93, 3.34, 0], 0.055, 0.042, 0.058, PALETTE.beakDeep);
  body.addEllipsoid([1.12, 3.57, 0.15], 0.022, 0.018, 0.018, PALETTE.beakDeep);

  /* Pouch: overlapping ellipsoids make a hanging curve under the bill. */
  body.addEllipsoid([1.41, 3.20, 0], 0.46, 0.20, 0.26, PALETTE.pouch, rotationZ(-0.10));
  body.addEllipsoid([1.09, 3.32, 0], 0.25, 0.16, 0.22, PALETTE.pouchLight, rotationZ(-0.04));
  body.addEllipsoid([1.70, 3.24, 0], 0.18, 0.12, 0.16, PALETTE.pouchLight);

  /* Eyes. */
  body.addEllipsoid([0.96, 3.66, 0.215], 0.058, 0.058, 0.038, PALETTE.eye);
  body.addEllipsoid([0.96, 3.66, -0.215], 0.058, 0.058, 0.038, PALETTE.eye);
  body.addEllipsoid([0.98, 3.68, 0.243], 0.017, 0.017, 0.011, PALETTE.eyeGlint);
  body.addEllipsoid([0.98, 3.68, -0.243], 0.017, 0.017, 0.011, PALETTE.eyeGlint);

  /* Helmet. */
  body.addEllipsoid([0.78, 3.76, 0], 0.33, 0.17, 0.28, PALETTE.helmet);
  body.addBox([1.05, 3.68, 0], 0.16, 0.03, 0.30, PALETTE.helmetDeep);
  body.addBox([0.78, 3.82, 0], 0.27, 0.02, 0.09, PALETTE.white);
  body.addEllipsoid([0.64, 3.85, 0], 0.075, 0.03, 0.09, PALETTE.helmetDeep);

  /* Scarf band and knot. */
  body.addTorus([0.40, 3.06, 0], 0.215, 0.050, PALETTE.scarf, rotationX(PI / 2));
  body.addEllipsoid([0.26, 2.99, 0.16], 0.085, 0.09, 0.075, PALETTE.scarfDeep);

  /* Tail feathers. */
  body.addEllipsoid([-0.68, 2.42, 0], 0.36, 0.08, 0.18, PALETTE.creamShade, rotationZ(0.22));
  body.addEllipsoid([-0.64, 2.30, 0.16], 0.30, 0.055, 0.10, PALETTE.gray, rotationZ(0.34));
  body.addEllipsoid([-0.64, 2.30, -0.16], 0.30, 0.055, 0.10, PALETTE.gray, rotationZ(0.34));

  return {
    body: body.build(),
    wingNear: buildWing(1),
    wingFar: buildWing(-1),
    dimensions: PELICAN,
  };
}

/**
 * One wing, built around its shoulder. Local +X points forward and local -Y
 * down; the host rotates the whole mesh around the shoulder to flap it.
 */
function buildWing(side) {
  const b = new MeshBuilder();
  const zDrop = -side * 0.19;

  /* A slim wing-arm: shoulder -> elbow -> handlebar grip. */
  b.addTube([0, 0, 0], [0.30, -0.32, zDrop * 0.4], 0.115, PALETTE.cream, 8);
  b.addTube([0.30, -0.32, zDrop * 0.4], [0.56, -0.63, zDrop], 0.082, PALETTE.cream, 8);
  b.addEllipsoid([0.30, -0.32, zDrop * 0.4], 0.125, 0.125, 0.115, PALETTE.creamShade);
  b.addEllipsoid([0.56, -0.63, zDrop], 0.085, 0.08, 0.07, PALETTE.gray);

  /* A small wing membrane and three grey primary feathers behind the elbow. */
  b.addEllipsoid([0.14, -0.30, zDrop * 0.35], 0.20, 0.15, 0.052, PALETTE.creamShade, rotationZ(-0.70));
  b.addEllipsoid([0.12, -0.46, zDrop * 0.5], 0.20, 0.065, 0.043, PALETTE.gray, rotationZ(-1.15));
  b.addEllipsoid([0.21, -0.38, zDrop * 0.65], 0.18, 0.055, 0.040, PALETTE.creamShade, rotationZ(-1.0));
  b.addEllipsoid([0.24, -0.52, zDrop * 0.55], 0.16, 0.05, 0.036, PALETTE.gray, rotationZ(-1.2));

  /* A soft shoulder cap. */
  b.addEllipsoid([0.01, -0.02, zDrop * 0.12], 0.17, 0.16, 0.145, PALETTE.white);
  return b.build();
}
