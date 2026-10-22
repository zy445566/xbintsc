/*
 * xbintsc runtime — asynchronous I/O offload API.
 *
 * Declared separately from `rt_internal.h` so the internal header stays within
 * the repository's file-size budget. Extensions that offload blocking work
 * include `rt_internal.h`, which includes this header.
 */
#ifndef XT_RT_ASYNC_IO_H
#define XT_RT_ASYNC_IO_H

#include "rt.h"

/*
 * Some host operations (file I/O, DNS, a blocking HTTP exchange) have no
 * portable readiness notification, so they run on a small worker pool and
 * report back to the main thread. The contract is strict: `run` executes on a
 * worker and may only touch plain C memory, while `complete` executes on the
 * main thread and may build/consume `xt_value`s.
 *
 * A job begins with this struct. `roots` keeps up to four values alive while
 * the job is queued or running: the slots are scanned by the collector and are
 * only written from the main thread. `next` links the work/completion queue and
 * is owned by the pool; callers never touch it. `release` (optional) frees the
 * job after `complete` returns.
 */
#define XT_ASYNC_ROOT_SLOTS 4

typedef struct xt_async_job xt_async_job;

struct xt_async_job {
  xt_async_job *next;
  xt_async_job *live_next;
  xt_async_job *live_prev;
  void (*run)(xt_async_job *job);
  void (*complete)(xt_async_job *job);
  void (*release)(xt_async_job *job);
  xt_value roots[XT_ASYNC_ROOT_SLOTS];
};

/** Queue `job` on a worker (never blocks the caller). Takes ownership. */
void xt_async_submit(xt_async_job *job);
/** Number of submitted jobs whose completion has not run yet. */
int xt_async_pending(void);
/** Run completed jobs and drop the loop watcher when the queue becomes idle. */
void xt_async_poll(void);

#endif /* XT_RT_ASYNC_IO_H */
