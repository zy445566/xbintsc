/*
 * xbintsc GUI engine — frame timing implementation (see `perf.h`).
 */

#include "perf.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xtgui {

PerfCounters g_perf_frame;
PerfCounters g_perf_total;
PerfTimings g_perf_frame_ms;
PerfTimings g_perf_last;

namespace {

int g_perf_mode = -1;  // -1 = not read yet, 0 = disabled, N = every N frames
uint64_t g_frame_start = 0;
/** Accumulator for `XT_GUI_PERF=<N>` (N > 1). */
PerfTimings g_agg_ms;
PerfCounters g_agg_counters;
uint64_t g_agg_frames = 0;

void readMode() {
  if (g_perf_mode >= 0) return;
  const char *value = SDL_getenv("XT_GUI_PERF");
  if (value == nullptr || value[0] == '\0' || std::strcmp(value, "0") == 0) {
    g_perf_mode = 0;
    return;
  }
  int every = std::atoi(value);
  g_perf_mode = every > 1 ? every : 1;
}

void addCounters(PerfCounters *target, const PerfCounters &value) {
  target->frames += value.frames;
  target->presents += value.presents;
  target->skippedFrames += value.skippedFrames;
  target->restyles += value.restyles;
  target->restyleNodes += value.restyleNodes;
  target->rulesEvaluated += value.rulesEvaluated;
  target->mediaEvaluated += value.mediaEvaluated;
  target->layouts += value.layouts;
  target->layoutBoxes += value.layoutBoxes;
  target->textShapeCalls += value.textShapeCalls;
  target->textShapeHits += value.textShapeHits;
  target->paints += value.paints;
  target->paintOps += value.paintOps;
  target->drawRuns += value.drawRuns;
  target->drawCalls += value.drawCalls;
  target->uploadSubmits += value.uploadSubmits;
  target->vertices += value.vertices;
  target->syncNodes += value.syncNodes;
  target->transitionFrames += value.transitionFrames;
  target->canvasDraws += value.canvasDraws;
  target->canvasUploads += value.canvasUploads;
  target->canvasUploadBytes += value.canvasUploadBytes;
  target->webglDraws += value.webglDraws;
  target->webglPasses += value.webglPasses;
  target->webglPipelineMisses += value.webglPipelineMisses;
  target->webglBufferUploadBytes += value.webglBufferUploadBytes;
}

void addTimings(PerfTimings *target, const PerfTimings &value) {
  target->frame += value.frame;
  target->restyle += value.restyle;
  target->layout += value.layout;
  target->paint += value.paint;
  target->upload += value.upload;
  target->draw += value.draw;
}

void printLine(uint64_t frames, const PerfTimings &ms, const PerfCounters &counters) {
  double divisor = frames > 0 ? (double)frames : 1.0;
  std::fprintf(stderr,
               "xt_gui perf frames=%llu ms=%.3f restyle=%.3f layout=%.3f paint=%.3f "
               "upload=%.3f draw=%.3f restyles=%llu nodes=%llu rules=%llu media=%llu "
               "layouts=%llu boxes=%llu text=%llu textHit=%llu ops=%llu runs=%llu "
               "draws=%llu uploads=%llu verts=%llu sync=%llu presents=%llu skip=%llu "
               "cdraw=%llu cups=%llu cbytes=%llu gldraw=%llu passes=%llu gmiss=%llu "
               "gbytes=%llu\n",
               (unsigned long long)frames, ms.frame / divisor, ms.restyle / divisor,
               ms.layout / divisor, ms.paint / divisor, ms.upload / divisor, ms.draw / divisor,
               (unsigned long long)counters.restyles, (unsigned long long)counters.restyleNodes,
               (unsigned long long)counters.rulesEvaluated,
               (unsigned long long)counters.mediaEvaluated, (unsigned long long)counters.layouts,
               (unsigned long long)counters.layoutBoxes,
               (unsigned long long)counters.textShapeCalls,
               (unsigned long long)counters.textShapeHits, (unsigned long long)counters.paintOps,
               (unsigned long long)counters.drawRuns, (unsigned long long)counters.drawCalls,
               (unsigned long long)counters.uploadSubmits, (unsigned long long)counters.vertices,
               (unsigned long long)counters.syncNodes, (unsigned long long)counters.presents,
               (unsigned long long)counters.skippedFrames,
               (unsigned long long)counters.canvasDraws,
               (unsigned long long)counters.canvasUploads,
               (unsigned long long)counters.canvasUploadBytes,
               (unsigned long long)counters.webglDraws,
               (unsigned long long)counters.webglPasses,
               (unsigned long long)counters.webglPipelineMisses,
               (unsigned long long)counters.webglBufferUploadBytes);
}

}  // namespace

bool xt_perf_active() {
  readMode();
  return g_perf_mode != 0;
}

uint64_t xt_perf_counter() { return SDL_GetPerformanceCounter(); }

double xt_perf_ms_since(uint64_t start) {
  uint64_t frequency = SDL_GetPerformanceFrequency();
  if (frequency == 0 || start == 0) return 0.0;
  return (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)frequency;
}

PerfScope::PerfScope(double *slot) {
  /* Always time the phases: five counter pairs per frame are free, and
   * `win.stats()` is then useful without the environment variable (which only
   * controls printing). */
  slot_ = slot;
  if (slot_ != nullptr) start_ = SDL_GetPerformanceCounter();
}

PerfScope::~PerfScope() {
  if (slot_ == nullptr) return;
  *slot_ += xt_perf_ms_since(start_);
}

void xt_perf_frame_begin() {
  g_perf_frame = PerfCounters();
  g_perf_frame_ms = PerfTimings();
  g_frame_start = SDL_GetPerformanceCounter();
}

void xt_perf_frame_end() {
  g_perf_frame_ms.frame = xt_perf_ms_since(g_frame_start);
  g_perf_last = g_perf_frame_ms;
  g_perf_frame.frames = 1;
  addCounters(&g_perf_total, g_perf_frame);

  readMode();
  if (g_perf_mode == 0) return;
  if (g_perf_mode == 1) {
    printLine(1, g_perf_frame_ms, g_perf_frame);
    return;
  }
  addTimings(&g_agg_ms, g_perf_frame_ms);
  addCounters(&g_agg_counters, g_perf_frame);
  g_agg_frames += 1;
  if (g_agg_frames < (uint64_t)g_perf_mode) return;
  printLine(g_agg_frames, g_agg_ms, g_agg_counters);
  g_agg_ms = PerfTimings();
  g_agg_counters = PerfCounters();
  g_agg_frames = 0;
}

}  // namespace xtgui
