/*
 * xbintsc runtime — WHATWG Streams.
 *
 * `ReadableStream`, `WritableStream`, `TransformStream`, their readers,
 * writers and controllers are available as global web-platform values and are
 * re-exported by Node's `stream/web` module. The implementation is a focused
 * subset: default readers/writers, queuing strategies, tee, pipeTo/pipeThrough,
 * async iteration and the text encoder/decoder transform streams.
 *
 * Byte streams (BYOB readers) and compression streams are not implemented; the
 * constructors for those classes throw an actionable TypeError, while `typeof`
 * still reports `"function"` so feature probes behave like a browser.
 *
 * The implementation is grouped by concern under `xt_streams/` and included
 * here as one translation unit, following `xt_fetch.c`.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xt_streams/common.inc"
#include "xt_streams/readable.inc"
#include "xt_streams/readable-extra.inc"
#include "xt_streams/writable.inc"
#include "xt_streams/transform.inc"
#include "xt_streams/encoding.inc"
#include "xt_streams/constructors.inc"
