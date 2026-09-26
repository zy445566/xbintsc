/*
 * xbintsc runtime — minimal POSIX ERE-compatible regular expressions.
 *
 * Windows does not ship <regex.h>, so on `_WIN32` targets we fall back to this
 * small self-contained matcher. It implements the subset the runtime uses:
 *
 *   literals, '.', '^', '$', '[abc]', '[^abc]', '[a-z]', '*', '+', '?',
 *   alternation '|', groups '(...)', and escapes ('\d', '\w', '\s', ...).
 *
 * Capturing groups are reported through `regmatch_t` the same way POSIX does:
 * `pmatch[0]` is the whole match and `pmatch[k]` the k-th group in pattern
 * order, with unmatched groups left at -1. The implementation is a recursive
 * backtracking matcher over a small AST; captures are carried on an immutable
 * chain of stack frames so abandoned search paths are undone automatically.
 * Patterns are tiny, so this is fast enough and keeps the runtime free of an
 * external dependency.
 *
 * The implementation is grouped by functional area under `xt_regex/` and
 * `#include`d here as a single translation unit, so the `static` helpers stay
 * private while each file stays focused on one area.
 */

#ifndef XT_REGEX_H
#define XT_REGEX_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "xt_regex/types.inc"
#include "xt_regex/helpers.inc"
#include "xt_regex/parser.inc"
#include "xt_regex/matcher.inc"
#include "xt_regex/api.inc"

#endif /* XT_REGEX_H */
