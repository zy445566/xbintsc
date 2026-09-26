/*
 * xbintsc runtime — exceptions and output.
 *
 * `try`/`catch` is a stack of setjmp frames; `xt_throw` longjmps into the
 * innermost frame when one is active and otherwise prints + exits. Also holds
 * `print`/`console` output and the Node-like value inspector.
 *
 * The generated IR saves each frame with a `setjmp`-family call that passes
 * the frame explicitly — the jmp_buf plus a frame pointer — because the call
 * is already IR and clang will not rewrite it. On Linux/macOS that is
 * `_setjmp(buf, frame)`; the extra argument is ignored. On Windows the UCRT
 * `_setjmp` stores the frame in `_JUMP_BUFFER.Frame` and `longjmp` passes it
 * to `RtlUnwind` to run the unwind, so omitting it makes `longjmp` unwind to a
 * bogus target (`STATUS_BAD_FUNCTION_TABLE`, 0xC00000FF). Windows ARM64 has
 * no `_setjmp` at all: the generated IR uses `_setjmpex(buf, entry-sp)` with
 * the stack pointer on entry (`@llvm.sponentry`), exactly as clang lowers a C
 * `setjmp` there. The C runtime (see `xt_try_setjmp`) calls clang's
 * `_setjmp` built-in so it gets the same frame injection (lowered to
 * `_setjmpex` on ARM64). `_setjmp`/`_setjmpex` are used rather than the
 * exported `setjmp` symbol, whose Windows ABI is an incompatible two-argument
 * routine; on Linux/macOS `_setjmp` takes only the buffer and pairs with
 * `longjmp` (the XSI `_longjmp` does not exist on Windows).
 *
 * The implementation is grouped by functional area under `xt_io/` and
 * `#include`d here as a single translation unit, so the exception frame state
 * and inspection helpers stay `static` while each file stays focused.
 */

#include "rt_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <winsock2.h>
#endif

#include "xt_io/exception.inc"
#include "xt_io/inspect.inc"
#include "xt_io/console.inc"
#include "xt_io/base64.inc"
