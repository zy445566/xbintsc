/*
 * xbintsc runtime — host event loop.
 *
 * A tiny `select(2)` based reactor that lets host extensions (node's `net`,
 * `dgram`, `http`, ...) deliver callbacks without baking an OS abstraction
 * into the compiler. Generated `main` calls `xt_run_event_loop()` once the
 * program body (and its microtasks) have finished; the loop returns as soon as
 * no file descriptors remain registered, so programs that never touch I/O are
 * unaffected.
 *
 * Extensions register descriptors with `xt_loop_add` and receive readiness
 * callbacks. The loop drains the promise microtask queue after every batch of
 * callbacks so `await`/`.then` chains keep working inside handlers.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define XT_LOOP_MAX_WATCHERS 1024

typedef struct {
  int fd;
  int events;
  xt_io_handler handler;
  void *userdata;
  int active;
} xt_loop_watcher;

static xt_loop_watcher xt_loop_watchers[XT_LOOP_MAX_WATCHERS];
static int xt_loop_watcher_count = 0;
static int xt_loop_active_count = 0;

static xt_loop_watcher *xt_loop_find(int fd) {
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    if (xt_loop_watchers[i].active && xt_loop_watchers[i].fd == fd) return &xt_loop_watchers[i];
  }
  return NULL;
}

int xt_loop_add(int fd, int events, xt_io_handler handler, void *userdata) {
  if (fd < 0 || !handler) return -1;
  xt_loop_watcher *existing = xt_loop_find(fd);
  if (existing) {
    existing->events = events;
    existing->handler = handler;
    existing->userdata = userdata;
    return 0;
  }
  if (xt_loop_watcher_count >= XT_LOOP_MAX_WATCHERS) return -1;
  xt_loop_watcher *watcher = &xt_loop_watchers[xt_loop_watcher_count++];
  watcher->fd = fd;
  watcher->events = events;
  watcher->handler = handler;
  watcher->userdata = userdata;
  watcher->active = 1;
  xt_loop_active_count++;
  return 0;
}

void xt_loop_update(int fd, int events) {
  xt_loop_watcher *watcher = xt_loop_find(fd);
  if (watcher) watcher->events = events;
}

void xt_loop_remove(int fd) {
  xt_loop_watcher *watcher = xt_loop_find(fd);
  if (!watcher) return;
  watcher->active = 0;
  watcher->fd = -1;
  xt_loop_active_count--;
}

/* A host (GUI toolkit, embedder) can take over the main loop; see
 * `xt_loop_set_main`. `NULL` keeps the default blocking behaviour. */
static xt_main_loop_fn xt_loop_main = NULL;

void xt_loop_set_main(xt_main_loop_fn fn) { xt_loop_main = fn; }

int xt_loop_poll(int timeout_ms) {
  if (xt_loop_active_count <= 0) return 0;

  fd_set readSet;
  fd_set writeSet;
  FD_ZERO(&readSet);
  FD_ZERO(&writeSet);
  int maxFd = -1;
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    xt_loop_watcher *watcher = &xt_loop_watchers[i];
    if (!watcher->active) continue;
    if (watcher->events & XT_IO_READ) FD_SET(watcher->fd, &readSet);
    if (watcher->events & XT_IO_WRITE) FD_SET(watcher->fd, &writeSet);
    if (watcher->fd > maxFd) maxFd = watcher->fd;
  }
  if (maxFd < 0) return 0;

  struct timeval timeout;
  struct timeval *timeoutPtr = NULL;
  if (timeout_ms >= 0) {
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    timeoutPtr = &timeout;
  }

  int ready = select(maxFd + 1, &readSet, &writeSet, NULL, timeoutPtr);
  if (ready < 0) return -1;
  if (ready == 0) return xt_loop_active_count;

  /* Collect the ready watchers first: handlers may add/remove descriptors. */
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    xt_loop_watcher *watcher = &xt_loop_watchers[i];
    if (!watcher->active) continue;
    int events = 0;
    if ((watcher->events & XT_IO_READ) && FD_ISSET(watcher->fd, &readSet)) events |= XT_IO_READ;
    if ((watcher->events & XT_IO_WRITE) && FD_ISSET(watcher->fd, &writeSet)) events |= XT_IO_WRITE;
    if (events) watcher->handler(watcher->userdata, events);
  }
  xt_drain_microtasks();
  return xt_loop_active_count;
}

void xt_run_event_loop(void) {
  /* A host that owns the main loop (GUI toolkit) takes precedence: it is
     responsible for calling `xt_loop_poll` while it runs. */
  if (xt_loop_main != NULL) {
    xt_loop_main();
    return;
  }
  while (xt_loop_active_count > 0) {
    if (xt_loop_poll(-1) < 0) break;
  }
}

int xt_loop_has_work(void) { return xt_loop_active_count > 0; }
