// Real `<video>` / `<audio>` playback in the xbintsc GUI engine.
//
// There is no WebView and no `<video controls>`: the optional `media` extension
// decodes through a vendored FFmpeg and the renderer uploads each frame as a GPU
// texture. The script below drives playback through the DOM API and draws its
// own progress bar over the video with CSS — the intended "API control + custom
// overlay" pattern.
//
// Build `media.a` once (`npm run media`) and enable both extensions. Run from
// the repository root, because the `src` paths in `media.html` are relative to
// the process working directory:
//
//   xbintsc run examples/gui/media.ts --ext gui,media
//
// `XT_GUI_AUTOCLOSE_MS` bounds the run when no display is available.

import { createWindow, run } from "gui";
import page from "./media.html";

const win = createWindow({ title: "xbintsc media", width: 560, height: 420 });

win.on("ready", () => {
  win.loadHTML(page);

  const video: any = win.document.getElementById("video");
  const audio: any = win.document.getElementById("audio");
  const bar: any = win.document.getElementById("bar");
  const toggle: any = win.document.getElementById("toggle");
  const mute: any = win.document.getElementById("mute");
  const back: any = win.document.getElementById("back");
  const fwd: any = win.document.getElementById("fwd");
  const label: any = win.document.getElementById("time");

  // The overlay bar polls the media clock; there is no built-in control UI.
  const render = () => {
    const duration = video.duration;
    const time = video.currentTime;
    if (duration > 0) {
      bar.style.setProperty("width", (time / duration) * 100 + "%");
      label.textContent = time + " / " + duration;
    }
  };

  video.addEventListener("loadedmetadata", () => {
    console.log("video = " + video.videoWidth + "x" + video.videoHeight + " duration=" + video.duration);
    console.log("audio duration=" + audio.duration);
    render();
  });
  video.addEventListener("play", () => {
    toggle.textContent = "Pause";
    console.log("play paused=" + video.paused);
  });
  video.addEventListener("pause", () => {
    toggle.textContent = "Play";
  });
  video.addEventListener("timeupdate", render);
  video.addEventListener("ended", () => console.log("ended"));
  video.addEventListener("error", () => console.log("error"));

  toggle.addEventListener("click", () => {
    if (video.paused) {
      video.play();
      audio.play();
    } else {
      video.pause();
      audio.pause();
    }
  });
  mute.addEventListener("click", () => {
    const next = !video.muted;
    video.muted = next;
    audio.muted = next;
    mute.textContent = next ? "Sound: off" : "Sound: on";
  });
  back.addEventListener("click", () => {
    const target = video.currentTime - 5;
    video.currentTime = target > 0 ? target : 0;
  });
  fwd.addEventListener("click", () => {
    video.currentTime = video.currentTime + 5;
  });

  // The example is interactive; close once it has played for a moment so a
  // scripted run terminates on its own.
  let frames = 0;
  const tick = () => {
    frames += 1;
    if (video.currentTime > 1.0 || frames > 600) {
      win.close();
      return;
    }
    win.requestAnimationFrame(tick);
  };
  win.requestAnimationFrame(tick);
});

win.on("close", () => console.log("closed"));
run();
