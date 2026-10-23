// 鹈鹕骑自行车（Canvas 2D）— the whole picture, its animation and its input.
//
// This file is the whole demo, shared unchanged by both hosts:
//
//   - a browser loads it as a classic `<script src="scene.js">` from
//     `index.html`, so opening the HTML directly just works;
//   - xbintsc reads the same `<script src>` through the gui extension's `.html`
//     loader, AOT-compiles the body and runs it in the native window.
//
// `window` and `document` are globals in a browser and parameters of the
// generated native script function, so the code below is ordinary Web code.
// `main.ts` only opens the native window and prints a report; the drawing,
// simulation, input and frame loop all live here.
//
// Every pixel is drawn with the gui extension's `<canvas>` 2D context:
// gradients, paths, ellipses, rounded rectangles, dashes, text and transforms —
// no images, no CSS artwork.
//
// The bike is drawn in a local coordinate system whose origin is the point on
// the road between the two wheel hubs. The wheels have radius 70, so their hubs
// sit at y = -70; the pelican rides above the saddle. `GROUND` is the screen y
// of that origin.

const WIDTH = 1000;
const HEIGHT = 640;
const GROUND = 516;
const BIKE_X = 438;
const WHEEL_R = 70;
const HUB_L = -118;
const HUB_R = 118;
const HUB_Y = -WHEEL_R;
const BB_X = -8;
const BB_Y = -72;
const SEAT_X = -92;
const SEAT_Y = -178;
const HEAD_X = 92;
const HEAD_Y = -176;
const TAU = Math.PI * 2;
/** Everything the simulation needs. `paints` caches the gradients created on
 * the first frame (creating one per frame would grow the canvas gradient table
 * for no benefit). */
const createScene = () => ({
    time: 0,
    scroll: 0,
    speed: 250,
    boost: false,
    brake: false,
    jumpY: 0,
    jumpV: 0,
    airborne: false,
    paused: false,
    distance: 0,
    paints: null
});
/** Advance the world by `dt` seconds. The ground scrolls under a bike that
 * stays put, so wheel/crank angles are derived from `scroll`, not from time. */
const updateScene = (state, dt) => {
    if (!(dt > 0))
        return;
    if (dt > 0.05)
        dt = 0.05;
    if (state.paused)
        return;
    state.time = state.time + dt;
    let target = 250;
    if (state.boost)
        target = 430;
    if (state.brake)
        target = 110;
    state.speed = state.speed + (target - state.speed) * Math.min(1, dt * 3.2);
    const dx = state.speed * dt;
    state.scroll = state.scroll + dx;
    state.distance = state.distance + dx / 18;
    if (state.airborne) {
        state.jumpV = state.jumpV + 1750 * dt;
        state.jumpY = state.jumpY + state.jumpV * dt;
        if (state.jumpY >= 0) {
            state.jumpY = 0;
            state.jumpV = 0;
            state.airborne = false;
        }
    }
};
const jump = (state) => {
    if (state.paused || state.airborne)
        return;
    state.airborne = true;
    state.jumpV = -740;
};
const resetScene = (state) => {
    state.scroll = 0;
    state.speed = 250;
    state.distance = 0;
    state.jumpY = 0;
    state.jumpV = 0;
    state.airborne = false;
};
/* -- small drawing helpers ------------------------------------------------- */
const circle = (ctx, x, y, r) => {
    ctx.beginPath();
    ctx.arc(x, y, r, 0, TAU);
    ctx.fill();
};
const ellipse = (ctx, x, y, rx, ry, rotation) => {
    ctx.beginPath();
    ctx.ellipse(x, y, rx, ry, rotation, 0, TAU);
    ctx.fill();
};
const strokeCircle = (ctx, x, y, r) => {
    ctx.beginPath();
    ctx.arc(x, y, r, 0, TAU);
    ctx.stroke();
};
const wrap = (value, span) => ((value % span) + span) % span;
const pad4 = (value) => {
    let text = String(Math.floor(value));
    while (text.length < 4)
        text = "0" + text;
    return text;
};
/** Stroke a polyline given as `[x0, y0, x1, y1, …]`. */
const polyline = (ctx, points) => {
    ctx.beginPath();
    ctx.moveTo(points[0], points[1]);
    for (let i = 2; i + 1 < points.length; i += 2)
        ctx.lineTo(points[i], points[i + 1]);
    ctx.stroke();
};
/* -- background ------------------------------------------------------------ */
const ensurePaints = (ctx, state) => {
    if (state.paints !== null)
        return state.paints;
    const sky = ctx.createLinearGradient(0, 0, 0, 486);
    sky.addColorStop(0, "#2f6fb3");
    sky.addColorStop(0.45, "#6fb3e0");
    sky.addColorStop(0.78, "#bfe3f5");
    sky.addColorStop(1, "#f6e7c8");
    const sun = ctx.createRadialGradient(686, 118, 8, 686, 118, 128);
    sun.addColorStop(0, "rgba(255,246,196,0.95)");
    sun.addColorStop(0.55, "rgba(255,225,140,0.35)");
    sun.addColorStop(1, "rgba(255,210,120,0)");
    const road = ctx.createLinearGradient(0, 490, 0, 640);
    road.addColorStop(0, "#4a5058");
    road.addColorStop(0.35, "#3b4149");
    road.addColorStop(1, "#2a2f36");
    state.paints = { sky: sky, sun: sun, road: road };
    return state.paints;
};
const drawCloud = (ctx, x, y, scale) => {
    ellipse(ctx, x, y, 52 * scale, 18 * scale, 0);
    ellipse(ctx, x - 30 * scale, y + 5 * scale, 30 * scale, 14 * scale, 0);
    ellipse(ctx, x + 34 * scale, y + 4 * scale, 34 * scale, 15 * scale, 0);
    ellipse(ctx, x + 6 * scale, y - 12 * scale, 26 * scale, 18 * scale, 0);
};
const drawTree = (ctx, x, y, scale) => {
    ctx.fillStyle = "#2f7a5b";
    ctx.fillRect(x - 2 * scale, y - 14 * scale, 4 * scale, 18 * scale);
    ctx.fillStyle = "#3e8f68";
    circle(ctx, x, y - 22 * scale, 13 * scale);
    circle(ctx, x - 12 * scale, y - 16 * scale, 10 * scale);
    circle(ctx, x + 12 * scale, y - 16 * scale, 10 * scale);
};
const drawBackdrop = (ctx, state) => {
    const paints = ensurePaints(ctx, state);
    ctx.fillStyle = paints.sky;
    ctx.fillRect(0, 0, WIDTH, 490);
    ctx.fillStyle = paints.sun;
    circle(ctx, 686, 118, 128);
    ctx.fillStyle = "#fff3c4";
    circle(ctx, 686, 118, 50);
    ctx.fillStyle = "#ffe9a3";
    circle(ctx, 686, 118, 38);
    ctx.save();
    ctx.globalAlpha = 0.85;
    ctx.fillStyle = "#ffffff";
    for (let i = 0; i < 5; i++) {
        /* The world moves left under a bike that rides right, so scenery wraps
           with a *negative* scroll term. */
        const x = wrap(i * 247 - state.scroll * 0.1, 1200) - 130;
        drawCloud(ctx, x, 74 + ((i * 47) % 110), 0.72 + (i % 3) * 0.16);
    }
    ctx.restore();
    /* Far and near hills: a sine silhouette scrolled at different speeds. */
    const hill = (base, amp, color, speed, phase) => {
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.moveTo(-40, HEIGHT);
        for (let x = -40; x <= WIDTH + 40; x += 20) {
            const y = base - amp * (0.55 + 0.45 * Math.sin((x + state.scroll * speed) * 0.006 + phase));
            ctx.lineTo(x, y);
        }
        ctx.lineTo(WIDTH + 40, HEIGHT);
        ctx.closePath();
        ctx.fill();
    };
    hill(452, 56, "#79c7a3", 0.2, 0.4);
    hill(488, 34, "#4f9d7c", 0.42, 2.1);
    for (let i = 0; i < 8; i++) {
        const x = wrap(i * 173 - state.scroll * 0.42, 1120) - 80;
        drawTree(ctx, x, 492 - (i % 3) * 4, 0.8 + (i % 2) * 0.24);
    }
    /* Verge and road. */
    ctx.fillStyle = "#7fbf63";
    ctx.fillRect(0, 468, WIDTH, 22);
    ctx.fillStyle = "#5f9e4a";
    ctx.fillRect(0, 486, WIDTH, 6);
    ctx.fillStyle = paints.road;
    ctx.fillRect(0, 490, WIDTH, 150);
    ctx.fillStyle = "#333941";
    ctx.fillRect(0, 490, WIDTH, 4);
    /* The centre line is a dashed stroke: setLineDash + lineDashOffset scroll
       the pattern with the road instead of drawing each dash by hand. */
    ctx.save();
    ctx.strokeStyle = "#f2f4f0";
    ctx.lineWidth = 6;
    ctx.lineCap = "butt";
    ctx.setLineDash([44, 38]);
    /* A positive offset shifts the pattern toward the path start (left), which
       is the direction the road should move under a bike heading right. */
    ctx.lineDashOffset = state.scroll % 82;
    ctx.beginPath();
    ctx.moveTo(-40, 596);
    ctx.lineTo(WIDTH + 40, 596);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
};
/* -- bike ------------------------------------------------------------------ */
const drawWheel = (ctx, x, y, angle) => {
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(angle);
    ctx.lineWidth = 14;
    ctx.strokeStyle = "#20262e";
    strokeCircle(ctx, 0, 0, WHEEL_R - 7);
    ctx.lineWidth = 4;
    ctx.strokeStyle = "#cfd8e3";
    strokeCircle(ctx, 0, 0, WHEEL_R - 16);
    ctx.lineWidth = 2;
    ctx.strokeStyle = "#9aa8b8";
    ctx.lineCap = "round";
    for (let i = 0; i < 8; i++) {
        const a = (i * TAU) / 8;
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(Math.cos(a) * (WHEEL_R - 18), Math.sin(a) * (WHEEL_R - 18));
        ctx.stroke();
    }
    ctx.fillStyle = "#ffd166";
    circle(ctx, 0, -(WHEEL_R - 11), 4);
    ctx.fillStyle = "#26313f";
    circle(ctx, 0, 0, 8);
    ctx.fillStyle = "#e63946";
    circle(ctx, 0, 0, 3);
    ctx.restore();
};
const strokeTubes = (ctx, tubes, color, width) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    for (let i = 0; i < tubes.length; i++)
        polyline(ctx, tubes[i]);
};
const drawFrame = (ctx) => {
    const tubes = [
        [HUB_L, HUB_Y, BB_X, BB_Y],
        [BB_X, BB_Y, HUB_R, HUB_Y],
        [HUB_L, HUB_Y, SEAT_X, SEAT_Y],
        [SEAT_X, SEAT_Y, BB_X, BB_Y],
        [SEAT_X, SEAT_Y, HEAD_X, HEAD_Y],
        [BB_X, BB_Y, HEAD_X, HEAD_Y],
        [HEAD_X, HEAD_Y, HUB_R, HUB_Y]
    ];
    strokeTubes(ctx, tubes, "#8f1d27", 13);
    strokeTubes(ctx, tubes, "#e63946", 8);
    /* Chain to the rear hub and the chainring/cog. */
    ctx.strokeStyle = "#6d757e";
    ctx.lineWidth = 2;
    polyline(ctx, [BB_X - 24, BB_Y - 8, HUB_L + 3, HUB_Y - 11]);
    polyline(ctx, [BB_X - 24, BB_Y + 8, HUB_L + 3, HUB_Y + 11]);
    ctx.strokeStyle = "#4a5568";
    ctx.lineWidth = 5;
    strokeCircle(ctx, BB_X, BB_Y, 24);
    ctx.lineWidth = 3;
    strokeCircle(ctx, HUB_L, HUB_Y, 10);
    /* Saddle and seat post. */
    ctx.fillStyle = "#26313f";
    ctx.beginPath();
    ctx.roundRect(SEAT_X - 34, SEAT_Y - 7, 64, 14, 7);
    ctx.fill();
    ctx.fillStyle = "#e63946";
    ctx.fillRect(SEAT_X - 28, SEAT_Y - 2, 54, 2);
    /* Handlebar, grip, bell. */
    ctx.strokeStyle = "#26313f";
    ctx.lineWidth = 8;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    polyline(ctx, [HEAD_X, HEAD_Y, HEAD_X + 8, HEAD_Y - 30, HEAD_X + 38, HEAD_Y - 36,
        HEAD_X + 50, HEAD_Y - 26]);
    ctx.strokeStyle = "#e63946";
    ctx.lineWidth = 9;
    polyline(ctx, [HEAD_X + 34, HEAD_Y - 37, HEAD_X + 52, HEAD_Y - 24]);
    ctx.fillStyle = "#ffd166";
    circle(ctx, HEAD_X + 26, HEAD_Y - 48, 8);
    ctx.fillStyle = "#fff3c4";
    circle(ctx, HEAD_X + 24, HEAD_Y - 50, 3);
};
const drawCrank = (ctx, angle) => {
    ctx.strokeStyle = "#26313f";
    ctx.lineWidth = 7;
    ctx.lineCap = "round";
    for (let i = 0; i < 2; i++) {
        const a = angle + i * Math.PI;
        polyline(ctx, [BB_X, BB_Y, BB_X + Math.cos(a) * 28, BB_Y + Math.sin(a) * 28]);
    }
    ctx.fillStyle = "#26313f";
    circle(ctx, BB_X, BB_Y, 9);
    ctx.fillStyle = "#e63946";
    circle(ctx, BB_X, BB_Y, 4);
};
/* -- pelican --------------------------------------------------------------- */
const drawPelican = (ctx, state) => {
    const bob = Math.sin(state.time * 5.5) * 2;
    const wing = Math.sin(state.time * 7) * 0.13 - 0.16;
    /* Tail feathers. */
    ctx.fillStyle = "#e8eef6";
    ctx.beginPath();
    ctx.moveTo(-158, -232 + bob);
    ctx.lineTo(-192, -248 + bob);
    ctx.lineTo(-188, -214 + bob);
    ctx.closePath();
    ctx.fill();
    /* Body: a rotated ellipse, with a shaded belly. */
    ctx.save();
    ctx.translate(-100, -236 + bob);
    ctx.rotate(-0.1);
    ctx.fillStyle = "#f7fbff";
    ellipse(ctx, 0, 0, 66, 48, 0);
    ctx.fillStyle = "#dbe6f0";
    ellipse(ctx, 8, 23, 52, 22, 0);
    ctx.restore();
    /* Neck and head. */
    ctx.strokeStyle = "#f7fbff";
    ctx.lineWidth = 36;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-58, -264 + bob);
    ctx.quadraticCurveTo(-38, -300 + bob, -12, -330 + bob);
    ctx.stroke();
    ctx.fillStyle = "#f7fbff";
    ellipse(ctx, -2, -342 + bob, 33, 27, 0);
    /* Upper mandible and the sagging throat pouch that makes it a pelican. */
    ctx.fillStyle = "#ff9f1c";
    ctx.beginPath();
    ctx.moveTo(20, -354 + bob);
    ctx.quadraticCurveTo(72, -362 + bob, 110, -346 + bob);
    ctx.quadraticCurveTo(84, -338 + bob, 30, -338 + bob);
    ctx.closePath();
    ctx.fill();
    ctx.fillStyle = "#ff9f1c";
    ctx.beginPath();
    ctx.moveTo(26, -340 + bob);
    ctx.quadraticCurveTo(38, -304 + bob, 74, -312 + bob);
    ctx.quadraticCurveTo(98, -318 + bob, 106, -344 + bob);
    ctx.quadraticCurveTo(70, -334 + bob, 26, -340 + bob);
    ctx.closePath();
    ctx.fill();
    ctx.fillStyle = "#ffd166";
    ctx.beginPath();
    ctx.moveTo(34, -338 + bob);
    ctx.quadraticCurveTo(44, -316 + bob, 72, -320 + bob);
    ctx.quadraticCurveTo(90, -324 + bob, 96, -340 + bob);
    ctx.quadraticCurveTo(64, -332 + bob, 34, -338 + bob);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = "#d97a00";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(20, -354 + bob);
    ctx.quadraticCurveTo(72, -362 + bob, 110, -346 + bob);
    ctx.stroke();
    /* Eye. */
    ctx.fillStyle = "#26313f";
    circle(ctx, 8, -350 + bob, 5);
    ctx.fillStyle = "#ffffff";
    circle(ctx, 9.5, -352 + bob, 1.8);
    /* Wing, flapping around the shoulder. */
    ctx.save();
    ctx.translate(-108, -246 + bob);
    ctx.rotate(-0.18 + wing);
    ctx.fillStyle = "#cfd8e3";
    ctx.beginPath();
    ctx.ellipse(0, 0, 46, 28, 0, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = "#aab9c9";
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.strokeStyle = "#9aa8b8";
    ctx.lineCap = "round";
    for (let i = 0; i < 3; i++) {
        ctx.beginPath();
        ctx.moveTo(-29 + i * 7, 8 + i * 5);
        ctx.quadraticCurveTo(0, 17 + i * 4, 31, 4 + i * 5);
        ctx.stroke();
    }
    ctx.restore();
    /* Scarf streaming behind the neck. */
    const flap = Math.sin(state.time * 8) * 12;
    ctx.strokeStyle = "#e63946";
    ctx.lineWidth = 9;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(-24, -324 + bob);
    ctx.bezierCurveTo(-74, -314 + bob, -100, -346 + flap + bob, -160, -324 + flap + bob);
    ctx.stroke();
    ctx.strokeStyle = "#ff8fb1";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(-27, -327 + bob);
    ctx.bezierCurveTo(-76, -319 + bob, -100, -341 + flap + bob, -154, -322 + flap + bob);
    ctx.stroke();
};
const drawLeg = (ctx, hipX, hipY, footX, footY, bend, color) => {
    const kneeX = (hipX + footX) / 2 + bend;
    const kneeY = (hipY + footY) / 2 - 4;
    ctx.strokeStyle = color;
    ctx.lineWidth = 10;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    polyline(ctx, [hipX, hipY, kneeX, kneeY, footX, footY]);
};
const drawPedal = (ctx, x, y, front) => {
    ctx.fillStyle = front ? "#26313f" : "#3d4a5a";
    ctx.beginPath();
    ctx.roundRect(x - 12, y - 3.5, 24, 7, 3);
    ctx.fill();
    ctx.fillStyle = front ? "#ffb703" : "#d97a00";
    ellipse(ctx, x, y - 4, 13, 7, -0.15);
};
const drawBike = (ctx, state) => {
    const lift = state.jumpY;
    /* Shadow on the road: it narrows as the bike jumps. */
    const shrink = Math.max(0.45, 1 + lift / 260);
    ctx.save();
    ctx.globalAlpha = 0.3 * shrink;
    ctx.fillStyle = "#1b2430";
    ellipse(ctx, BIKE_X, GROUND + 8, 150 * shrink, 14 * shrink, 0);
    ctx.restore();
    ctx.save();
    ctx.translate(BIKE_X, GROUND + lift);
    const wheelAngle = state.scroll / WHEEL_R;
    const crankAngle = wheelAngle * 0.55;
    drawWheel(ctx, HUB_L, HUB_Y, wheelAngle);
    drawWheel(ctx, HUB_R, HUB_Y, wheelAngle + 0.7);
    drawFrame(ctx);
    drawCrank(ctx, crankAngle);
    const footLX = BB_X + Math.cos(crankAngle + Math.PI) * 28;
    const footLY = BB_Y + Math.sin(crankAngle + Math.PI) * 28;
    const footRX = BB_X + Math.cos(crankAngle) * 28;
    const footRY = BB_Y + Math.sin(crankAngle) * 28;
    drawPelican(ctx, state);
    drawLeg(ctx, -100, -206, footLX, footLY, -15, "#c46a00");
    drawLeg(ctx, -72, -206, footRX, footRY, 15, "#e08a00");
    drawPedal(ctx, footLX, footLY, false);
    drawPedal(ctx, footRX, footRY, true);
    ctx.restore();
    /* Speed streaks behind the rider while boosting. */
    if (state.speed > 330) {
        const alpha = Math.min(0.35, (state.speed - 330) / 260);
        ctx.save();
        ctx.strokeStyle = "rgba(255,255,255," + alpha.toFixed(2) + ")";
        ctx.lineWidth = 4;
        ctx.lineCap = "round";
        for (let i = 0; i < 5; i++) {
            const y = 250 + i * 44;
            const len = 40 + ((i * 37 + Math.floor(state.scroll)) % 70);
            polyline(ctx, [BIKE_X - 190 - len, y, BIKE_X - 190, y]);
        }
        ctx.restore();
    }
};
/* -- HUD ------------------------------------------------------------------- */
const drawHud = (ctx, state) => {
    ctx.save();
    ctx.textBaseline = "alphabetic";
    ctx.textAlign = "left";
    ctx.fillStyle = "#ffffff";
    ctx.font = "bold 34px sans-serif";
    ctx.fillText("鹈鹕骑自行车", 36, 68);
    ctx.fillStyle = "#cfeaff";
    ctx.font = "14px sans-serif";
    ctx.fillText("PELICAN ON A BICYCLE · CANVAS 2D", 38, 92);
    ctx.fillStyle = "rgba(9,15,24,0.58)";
    ctx.beginPath();
    ctx.roundRect(736, 28, 228, 88, 18);
    ctx.fill();
    ctx.strokeStyle = "rgba(143,211,255,0.35)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(736, 28, 228, 88, 18);
    ctx.stroke();
    ctx.fillStyle = "#8fd3ff";
    ctx.font = "bold 13px sans-serif";
    ctx.fillText("DISTANCE", 758, 54);
    ctx.fillStyle = "#ffd166";
    ctx.font = "bold 32px sans-serif";
    ctx.fillText(pad4(state.distance) + " m", 756, 90);
    ctx.fillStyle = "#7d90a8";
    ctx.font = "12px sans-serif";
    ctx.fillText("SPEED " + Math.round(state.speed / 10) + " km/h", 758, 106);
    ctx.fillStyle = "rgba(255,255,255,0.78)";
    ctx.font = "13px sans-serif";
    ctx.fillText("SPACE 跳跃   ← → 加减速   R 重新开始   P 暂停   ESC 退出", 36, 614);
    ctx.restore();
};
/* -- frame ----------------------------------------------------------------- */
const drawScene = (ctx, state) => {
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, WIDTH, HEIGHT);
    drawBackdrop(ctx, state);
    drawBike(ctx, state);
    drawHud(ctx, state);
};

/* -- host ------------------------------------------------------------------ */

// The drawing and simulation above; this block is the host glue. In a browser
// `window`/`document` are globals, while xbintsc wraps the same body in
// `(window, document) => { … }` and runs it after the native document parses.
const canvas = document.getElementById("stage");
const ctx = canvas.getContext("2d");
const state = createScene();
let frames = 0;
let last = 0;

const isSpace = (event) => event.key === " " || event.key === "Space" ||
    event.key === "Up" || event.key === "ArrowUp" ||
    event.code === "Space" || event.code === "ArrowUp";
const isRight = (event) => event.key === "Right" || event.key === "ArrowRight" ||
    event.code === "ArrowRight";
const isLeft = (event) => event.key === "Left" || event.key === "ArrowLeft" ||
    event.code === "ArrowLeft";
const isLetter = (event, letter) => event.key === letter ||
    event.key === letter.toLowerCase() || event.code === "Key" + letter;

const onKeyDown = (event) => {
    let handled = true;
    if (isSpace(event)) {
        jump(state);
    } else if (isRight(event)) {
        state.boost = true;
    } else if (isLeft(event)) {
        state.brake = true;
    } else if (isLetter(event, "R")) {
        resetScene(state);
    } else if (isLetter(event, "P")) {
        state.paused = !state.paused;
    } else if (event.key === "Escape" || event.code === "Escape") {
        /* Native windows can be closed; a browser tab is not ours to close. */
        if (window.driver !== undefined) window.close();
    } else {
        handled = false;
    }
    if (handled && event.preventDefault !== undefined) event.preventDefault();
};

const onKeyUp = (event) => {
    if (isRight(event)) state.boost = false;
    if (isLeft(event)) state.brake = false;
};

window.addEventListener("keydown", onKeyDown);
window.addEventListener("keyup", onKeyUp);

const info = () => ({
    canvas: canvas.width + "x" + canvas.height,
    frames: frames,
    speed: state.speed,
    distance: state.distance,
    airborne: state.airborne,
    paused: state.paused,
    scroll: state.scroll
});

const probe = (x, y) => {
    const data = ctx.getImageData(x, y, 1, 1).data;
    return data[0] + "," + data[1] + "," + data[2] + "," + data[3];
};

const loop = (time) => {
    if (window.closed) return;
    let dt = last > 0 ? (time - last) / 1000 : 0.016;
    last = time;
    if (dt < 0) dt = 0;
    if (dt > 0.05) dt = 0.05;
    frames = frames + 1;
    updateScene(state, dt);
    drawScene(ctx, state);
    window.requestAnimationFrame(loop);
};

// Published for the native host's report and for a browser console:
// `window.__pelicanCanvas.info()` / `.probe(x, y)` / `.state`.
window["__pelicanCanvas"] = {
    info: info,
    probe: probe,
    state: state,
    jump: () => jump(state),
    reset: () => resetScene(state),
    pause: () => { state.paused = !state.paused; },
    step: (dt) => { updateScene(state, dt); drawScene(ctx, state); },
    draw: () => drawScene(ctx, state)
};

drawScene(ctx, state);
window.requestAnimationFrame(loop);
