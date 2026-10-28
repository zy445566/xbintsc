/*
 * xbintsc GUI engine — frame counters and phase timings.
 *
 * The engine is a single-threaded UI loop, so one process-wide record is
 * enough. `xt_gui_render_window` brackets every frame with
 * `xt_perf_frame_begin()` / `xt_perf_frame_end()`; the rest of the engine only
 * bumps counters or wraps a phase in `PerfScope`. `win.stats()` exposes the
 * last frame plus the cumulative totals, which is what
 * `tests/e2e/gui-perf.test.ts` asserts on (wall-clock times are informational
 * only, so the suite also runs on a software GPU in CI).
 *
 * Set `XT_GUI_PERF=1` to print one line per frame, or `XT_GUI_PERF=<N>` to
 * print an aggregated line every N frames (average of the timings, sums of the
 * counters). The counters and phase timings are always recorded (a few counter
 * reads per frame), so `win.stats()` is useful without the variable; the
 * environment variable only controls the stderr printing.
 */
#ifndef XT_GUI_PERF_H
#define XT_GUI_PERF_H

#include <cstdint>

namespace xtgui {

/** Counters for one frame; the same layout is reused for cumulative totals. */
struct PerfCounters {
  uint64_t frames = 0;
  uint64_t presents = 0;
  uint64_t skippedFrames = 0;
  uint64_t restyles = 0;
  uint64_t restyleNodes = 0;
  uint64_t rulesEvaluated = 0;
  uint64_t mediaEvaluated = 0;
  uint64_t layouts = 0;
  uint64_t layoutBoxes = 0;
  uint64_t textShapeCalls = 0;
  uint64_t textShapeHits = 0;
  uint64_t paints = 0;
  uint64_t paintOps = 0;
  uint64_t drawRuns = 0;
  uint64_t drawCalls = 0;
  uint64_t uploadSubmits = 0;
  uint64_t vertices = 0;
  uint64_t syncNodes = 0;
  uint64_t transitionFrames = 0;
  /* Canvas 2D: CPU draws, texture uploads and the bytes they move. A canvas
   * that is only redrawn in a small region should show a small `bytes` count. */
  uint64_t canvasDraws = 0;
  uint64_t canvasUploads = 0;
  uint64_t canvasUploadBytes = 0;
  /* WebGL: draw calls, render passes actually opened (a draw that has nothing
   * to upload must not open a new pass), pipeline-cache misses and the bytes
   * buffer updates copy to the GPU. */
  uint64_t webglDraws = 0;
  uint64_t webglPasses = 0;
  uint64_t webglPipelineMisses = 0;
  uint64_t webglBufferUploadBytes = 0;
};

/** Wall-clock phase timings in milliseconds. */
struct PerfTimings {
  double frame = 0;
  double restyle = 0;
  double layout = 0;
  double paint = 0;
  double upload = 0;
  double draw = 0;
};

/** The frame being recorded; reset by `xt_perf_frame_begin`. */
extern PerfCounters g_perf_frame;
/** Every frame since startup. */
extern PerfCounters g_perf_total;
/** Phase timings of the frame being recorded. */
extern PerfTimings g_perf_frame_ms;
/** Phase timings of the last completed frame. */
extern PerfTimings g_perf_last;

/** True when `XT_GUI_PERF` asks for timings/printing. Cached after first use. */
bool xt_perf_active();

/** Raw performance counter, for `PerfScope` / ad-hoc measurements. */
uint64_t xt_perf_counter();
/** Milliseconds elapsed since `start` (from `xt_perf_counter`). */
double xt_perf_ms_since(uint64_t start);

void xt_perf_frame_begin();
void xt_perf_frame_end();

/** Accumulate elapsed milliseconds into `*slot` while in scope. A no-op (beyond
 * one branch) when `XT_GUI_PERF` is unset. */
class PerfScope {
 public:
  explicit PerfScope(double *slot);
  ~PerfScope();
  PerfScope(const PerfScope &) = delete;
  PerfScope &operator=(const PerfScope &) = delete;

 private:
  double *slot_ = nullptr;
  uint64_t start_ = 0;
};

}  // namespace xtgui

#endif  // XT_GUI_PERF_H
