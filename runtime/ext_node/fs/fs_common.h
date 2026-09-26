/*
 * Shared helpers for the xbintsc Node `fs` runtime.
 *
 * The `fs` builtins live in several translation units (read_file.c,
 * write_file.c, fs_ops.c) but agree on encoding handling and error reporting
 * through these static inline helpers. Encoding support is deliberately
 * buffer-free: `base64`/`hex` are encoded to strings directly, since xbintsc
 * has no `Buffer` value type yet.
 *
 * The helpers are grouped by functional area under `fs/parts/` and
 * `#include`d here, so this stays the single header every `fs` translation
 * unit already pulls in.
 */
#ifndef XT_NODE_FS_COMMON_H
#define XT_NODE_FS_COMMON_H

#include "rt_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "parts/platform.inc"
#include "parts/dir.inc"
#include "parts/errors.inc"
#include "parts/time.inc"
#include "parts/flags.inc"
#include "parts/builtins.inc"
#include "parts/promisify.inc"
#include "parts/buffer-bridge.inc"
#include "parts/encoding.inc"

#endif /* XT_NODE_FS_COMMON_H */
