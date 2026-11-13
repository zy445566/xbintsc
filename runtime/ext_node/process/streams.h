/*
 * The `process.stdin` / `process.stdout` / `process.stderr` singletons.
 *
 * Declared separately from `process.c` so the module stays within the
 * repository's file-size budget; both translation units are compiled by
 * `src/extensions/node/process/index.ts`.
 */
#ifndef XT_PROCESS_STREAMS_H
#define XT_PROCESS_STREAMS_H

#include "rt.h"

/** The cached `process.stdout` object (an EventEmitter with `write`). */
xt_value xt_process_stdout_object(void);
/** The cached `process.stderr` object. */
xt_value xt_process_stderr_object(void);
/** The cached `process.stdin` object (an EventEmitter feeding "data" chunks). */
xt_value xt_process_stdin_object(void);

#endif /* XT_PROCESS_STREAMS_H */
