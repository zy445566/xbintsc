/*
 * xbintsc runtime — exceptions and output.
 *
 * `try`/`catch` is a stack of setjmp frames; `xt_throw` longjmps into the
 * innermost frame when one is active and otherwise prints + exits. Also holds
 * `print`/`console` output and the Node-like value inspector.
 */

#include "rt_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Exceptions and output                                                     */
/* ------------------------------------------------------------------------- */

/*
 * `try`/`catch` is implemented with a stack of setjmp frames. The compiler
 * allocates a frame with `xt_try_enter`, calls `_setjmp` on it, and pops it
 * with `xt_try_leave`. `xt_throw` longjmps into the innermost frame when one
 * is active, and only prints + exits when the exception is uncaught.
 */
typedef struct xt_try_frame {
  jmp_buf buf;
  struct xt_try_frame *prev;
  xt_value exception;
} xt_try_frame;

static xt_try_frame *g_try_top = NULL;

void *xt_try_enter(void) {
  xt_try_frame *frame = (xt_try_frame *)calloc(1, sizeof(xt_try_frame));
  if (!frame) abort();
  frame->prev = g_try_top;
  frame->exception = XT_UNDEFINED;
  g_try_top = frame;
  return (void *)frame;
}

xt_value xt_try_exception(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  return frame ? frame->exception : XT_UNDEFINED;
}

void xt_try_leave(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  if (frame && g_try_top == frame) g_try_top = frame->prev;
}

void xt_throw(xt_value v) {
  if (g_try_top) {
    g_try_top->exception = v;
    _longjmp(g_try_top->buf, 1);
  }
  xt_value message = xt_to_string(v);
  xt_string *s = xt_as_string(message);
  fprintf(stderr, "Uncaught %s\n", s->data);
  exit(1);
}

static void xt_print_value(xt_value v, FILE *out) {
  if (XT_IS_STRING(v)) {
    xt_string *s = xt_as_string(v);
    fwrite(s->data, 1, s->length, out);
    return;
  }
  xt_value text = xt_to_string(v);
  xt_string *s = xt_as_string(text);
  fwrite(s->data, 1, s->length, out);
}

void xt_print(xt_value v) { xt_print_value(v, stdout); }
void xt_println(xt_value v) {
  xt_print_value(v, stdout);
  fputc('\n', stdout);
}

/*
 * console.log applies a Node-like inspection format: strings unquoted at the
 * top level, arrays as `[ a, b ]` and objects as `{ key: value }`.
 */
static void xt_inspect(xt_value v, FILE *out) {
  if (XT_IS_ARRAY(v)) {
    xt_array *array = (xt_array *)XT_GET_PTR(v);
    fputc('[', out);
    for (uint32_t i = 0; i < array->length; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      xt_inspect(array->items[i], out);
    }
    if (array->length > 0) fputc(' ', out);
    fputc(']', out);
    return;
  }
  if (XT_IS_OBJECT(v)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(v);
    fputc('{', out);
    for (uint32_t i = 0; i < obj->count; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      fwrite(obj->properties[i].key->data, 1, obj->properties[i].key->length, out);
      fputs(": ", out);
      xt_inspect(obj->properties[i].value, out);
    }
    if (obj->count > 0) fputc(' ', out);
    fputc('}', out);
    return;
  }
  xt_print_value(v, out);
}

void xt_console_log(int32_t argc, xt_value *argv) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', stdout);
    xt_inspect(argv[i], stdout);
  }
  fputc('\n', stdout);
}

static void xt_console_write(int32_t argc, xt_value *argv, FILE *out) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', out);
    xt_inspect(argv[i], out);
  }
  fputc('\n', out);
}

void xt_console_info(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stdout); }
void xt_console_warn(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }
void xt_console_error(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }
