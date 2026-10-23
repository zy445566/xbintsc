/*
 * xbintsc runtime — asynchronous I/O offload pool.
 *
 * JavaScript's `await` can suspend an `async` body on a pending promise, but
 * the host still has to provide the work that eventually settles it. Sockets
 * and timers are readiness based and live directly on the `select(2)` reactor;
 * file I/O and DNS have no portable readiness notification, so they run on a
 * small pool of worker threads and report back through the same reactor.
 *
 * The threading contract is the important part:
 *
 *   - `job->run` executes on a worker thread and may only touch plain C memory
 *     (paths, buffers, syscalls). It must never call the `xt_*` value API, the
 *     collector, `xt_throw`, or the event loop.
 *   - `job->complete` executes on the main thread after the reactor wakes up.
 *     That is where results become `xt_value`s, promises settle, and a job may
 *     submit follow-up work (fetch redirects do exactly that).
 *   - `job->roots` keeps values needed across the suspension alive. They are
 *     written on the main thread and scanned by the collector like any other
 *     root.
 *
 * Completion wakes the reactor through a self-pipe (POSIX) or a loopback UDP
 * socket (Windows, whose `select` only understands sockets). The reactor also
 * polls the pool every iteration, so a failed wakeup degrades to a short wait
 * rather than a hang.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

#define XT_ASYNC_WORKERS 4
/* A fallback bound for the reactor wait while work is outstanding. With a
 * working wakeup the wait ends immediately when a job completes. */
#define XT_ASYNC_POLL_MS 10

static xt_async_job *xt_async_work_head = NULL;
static xt_async_job *xt_async_work_tail = NULL;
static xt_async_job *xt_async_done_head = NULL;
static xt_async_job *xt_async_done_tail = NULL;

/* Every submitted, not-yet-released job. Mutated only on the main thread. */
static xt_async_job *xt_async_live_head = NULL;

static int xt_async_pending_jobs = 0;
static int xt_async_workers_started = 0;
static int xt_async_worker_count = 0;
static int xt_async_roots_registered = 0;
static int xt_async_watcher_fd = -1;

#if defined(_WIN32)
/* Statically initialised like the POSIX primitives below: the reactor polls
 * the pool on every timer/socket iteration, even when no async job was ever
 * submitted, so the lock must be usable before `xt_async_submit` runs. */
static SRWLOCK xt_async_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE xt_async_cond = CONDITION_VARIABLE_INIT;
static SOCKET xt_async_wake_socket = INVALID_SOCKET;
#define XT_ASYNC_LOCK() AcquireSRWLockExclusive(&xt_async_lock)
#define XT_ASYNC_UNLOCK() ReleaseSRWLockExclusive(&xt_async_lock)
static void xt_async_cond_wait(void) {
  SleepConditionVariableSRW(&xt_async_cond, &xt_async_lock, INFINITE, 0);
}
static void xt_async_cond_signal(void) { WakeConditionVariable(&xt_async_cond); }
#else
static pthread_mutex_t xt_async_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t xt_async_cond = PTHREAD_COND_INITIALIZER;
static int xt_async_wake_pipe[2] = {-1, -1};
#define XT_ASYNC_LOCK() pthread_mutex_lock(&xt_async_lock)
#define XT_ASYNC_UNLOCK() pthread_mutex_unlock(&xt_async_lock)
static void xt_async_cond_wait(void) { pthread_cond_wait(&xt_async_cond, &xt_async_lock); }
static void xt_async_cond_signal(void) { pthread_cond_signal(&xt_async_cond); }
#endif

static void xt_async_scan_roots(void);

/* -- wakeup channel ------------------------------------------------------- */

static int xt_async_wake_fd(void) {
#if defined(_WIN32)
  return (int)xt_async_wake_socket;
#else
  return xt_async_wake_pipe[0];
#endif
}

static void xt_async_wake_create(void) {
#if defined(_WIN32)
  if (xt_async_wake_socket != INVALID_SOCKET) return;
  SOCKET socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd == INVALID_SOCKET) return;
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    closesocket(socket_fd);
    return;
  }
  int length = (int)sizeof(address);
  if (getsockname(socket_fd, (struct sockaddr *)&address, &length) != 0 ||
      connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    closesocket(socket_fd);
    return;
  }
  u_long nonBlocking = 1;
  ioctlsocket(socket_fd, FIONBIO, &nonBlocking);
  xt_async_wake_socket = socket_fd;
#else
  if (xt_async_wake_pipe[0] >= 0) return;
  if (pipe(xt_async_wake_pipe) != 0) {
    xt_async_wake_pipe[0] = -1;
    xt_async_wake_pipe[1] = -1;
    return;
  }
  for (int i = 0; i < 2; i++) {
    int flags = fcntl(xt_async_wake_pipe[i], F_GETFL, 0);
    if (flags >= 0) fcntl(xt_async_wake_pipe[i], F_SETFL, flags | O_NONBLOCK);
  }
#endif
}

/* Called by a worker after it queues a completion. */
static void xt_async_wakeup(void) {
#if defined(_WIN32)
  if (xt_async_wake_socket == INVALID_SOCKET) return;
  char byte = 1;
  send(xt_async_wake_socket, &byte, 1, 0);
#else
  if (xt_async_wake_pipe[1] < 0) return;
  char byte = 1;
  ssize_t ignored = write(xt_async_wake_pipe[1], &byte, 1);
  (void)ignored;
#endif
}

static void xt_async_wake_drain(void) {
#if defined(_WIN32)
  if (xt_async_wake_socket == INVALID_SOCKET) return;
  char buffer[256];
  while (recv(xt_async_wake_socket, buffer, (int)sizeof(buffer), 0) > 0) {
  }
#else
  if (xt_async_wake_pipe[0] < 0) return;
  char buffer[256];
  while (read(xt_async_wake_pipe[0], buffer, sizeof(buffer)) > 0) {
  }
#endif
}

static void xt_async_wake_handler(void *userdata, int events) {
  (void)userdata;
  (void)events;
  xt_async_poll();
}

/* -- queue helpers (lock held) -------------------------------------------- */

static void xt_async_push_work(xt_async_job *job) {
  job->next = NULL;
  if (xt_async_work_tail) xt_async_work_tail->next = job;
  else xt_async_work_head = job;
  xt_async_work_tail = job;
}

static void xt_async_push_done(xt_async_job *job) {
  job->next = NULL;
  if (xt_async_done_tail) xt_async_done_tail->next = job;
  else xt_async_done_head = job;
  xt_async_done_tail = job;
}

static xt_async_job *xt_async_pop_work(void) {
  xt_async_job *job = xt_async_work_head;
  if (!job) return NULL;
  xt_async_work_head = job->next;
  if (!xt_async_work_head) xt_async_work_tail = NULL;
  return job;
}

static xt_async_job *xt_async_pop_done(void) {
  xt_async_job *job = xt_async_done_head;
  if (!job) return NULL;
  xt_async_done_head = job->next;
  if (!xt_async_done_head) xt_async_done_tail = NULL;
  return job;
}

/* -- live job registry (main thread only) --------------------------------- */

static void xt_async_live_add(xt_async_job *job) {
  job->live_prev = NULL;
  job->live_next = xt_async_live_head;
  if (xt_async_live_head) xt_async_live_head->live_prev = job;
  xt_async_live_head = job;
}

static void xt_async_live_remove(xt_async_job *job) {
  if (job->live_prev) job->live_prev->live_next = job->live_next;
  else xt_async_live_head = job->live_next;
  if (job->live_next) job->live_next->live_prev = job->live_prev;
  job->live_next = NULL;
  job->live_prev = NULL;
}

static void xt_async_scan_roots(void) {
  for (xt_async_job *job = xt_async_live_head; job; job = job->live_next) {
    for (int i = 0; i < XT_ASYNC_ROOT_SLOTS; i++) xt_gc_mark_value(job->roots[i]);
  }
}

static void xt_async_register_roots(void) {
  if (xt_async_roots_registered) return;
  xt_async_roots_registered = 1;
  xt_gc_register_root_provider(xt_async_scan_roots);
}

/* -- worker threads -------------------------------------------------------- */

static void xt_async_worker_run(void) {
  for (;;) {
    XT_ASYNC_LOCK();
    xt_async_job *job = xt_async_pop_work();
    while (!job) {
      xt_async_cond_wait();
      job = xt_async_pop_work();
    }
    XT_ASYNC_UNLOCK();
    if (job->run) job->run(job);
    XT_ASYNC_LOCK();
    xt_async_push_done(job);
    XT_ASYNC_UNLOCK();
    xt_async_wakeup();
  }
}

#if defined(_WIN32)
static unsigned __stdcall xt_async_worker_entry(void *argument) {
  (void)argument;
  xt_async_worker_run();
  return 0;
}
#else
static void *xt_async_worker_entry(void *argument) {
  (void)argument;
  xt_async_worker_run();
  return NULL;
}
#endif

/* Spawn the pool lazily. Called with the lock held so a worker that starts
 * immediately blocks until the queue is published. */
static void xt_async_start_workers(void) {
  if (xt_async_workers_started) return;
  xt_async_workers_started = 1;
  for (int i = 0; i < XT_ASYNC_WORKERS; i++) {
#if defined(_WIN32)
    uintptr_t handle = _beginthreadex(NULL, 0, xt_async_worker_entry, NULL, 0, NULL);
    if (handle == 0) break;
    CloseHandle((HANDLE)handle);
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, xt_async_worker_entry, NULL) != 0) break;
    pthread_detach(thread);
#endif
    xt_async_worker_count++;
  }
}

/* -- public API ------------------------------------------------------------ */

int xt_async_pending(void) { return xt_async_pending_jobs; }

void xt_async_submit(xt_async_job *job) {
  if (!job || !job->run) {
    if (job && job->release) job->release(job);
    else free(job);
    return;
  }
  xt_async_register_roots();
  xt_async_live_add(job);
  xt_async_pending_jobs++;

  xt_async_wake_create();
  if (xt_async_watcher_fd < 0 && xt_async_wake_fd() >= 0) {
    if (xt_loop_add(xt_async_wake_fd(), XT_IO_READ, xt_async_wake_handler, NULL) == 0) {
      xt_async_watcher_fd = xt_async_wake_fd();
    }
  }

  XT_ASYNC_LOCK();
  xt_async_start_workers();
  if (xt_async_worker_count > 0) {
    xt_async_push_work(job);
    xt_async_cond_signal();
    XT_ASYNC_UNLOCK();
    return;
  }
  /* No worker could be created: complete on the reactor's next poll. */
  xt_async_push_done(job);
  XT_ASYNC_UNLOCK();
}

void xt_async_poll(void) {
  if (xt_async_watcher_fd >= 0) xt_async_wake_drain();
  for (;;) {
    XT_ASYNC_LOCK();
    xt_async_job *job = xt_async_pop_done();
    XT_ASYNC_UNLOCK();
    if (!job) break;
    if (job->complete) job->complete(job);
    xt_async_live_remove(job);
    if (job->release) job->release(job);
    else free(job);
    xt_async_pending_jobs--;
  }
  if (xt_async_pending_jobs == 0 && xt_async_watcher_fd >= 0) {
    xt_loop_remove(xt_async_watcher_fd);
    xt_async_watcher_fd = -1;
  }
}
