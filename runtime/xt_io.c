/*
 * xbintsc runtime — exceptions and output.
 *
 * `try`/`catch` is a stack of setjmp frames; `xt_throw` longjmps into the
 * innermost frame when one is active and otherwise prints + exits. Also holds
 * `print`/`console` output and the Node-like value inspector.
 *
 * The generated IR calls `_setjmp` with two arguments — the jmp_buf and the
 * caller's frame address (`@llvm.frameaddress(0)`) — paired with `longjmp`
 * here. This is exactly what clang lowers a C `_setjmp(buf)` call to on MSVC.
 * On Windows the UCRT `_setjmp` takes the frame as a second argument and
 * stores it in `_JUMP_BUFFER.Frame`; `longjmp` passes that frame to
 * `RtlUnwind` to run the unwind. Calling `_setjmp` with only one argument left
 * `Frame` as garbage, so `longjmp` unwound to a bogus target
 * (`STATUS_BAD_FUNCTION_TABLE`, 0xC00000FF). `_setjmp` is used rather than the
 * exported `setjmp` symbol, which is a legacy two-argument routine with an
 * incompatible ABI; on Linux/macOS `_setjmp` simply ignores the extra
 * argument and pairs with `longjmp` (the XSI `_longjmp` does not exist on
 * Windows).
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

/* Program arguments, captured once by `xt_set_program_args` from `main`. */
int32_t xt_program_argc = 0;
char **xt_program_argv = NULL;

void xt_set_program_args(int32_t argc, char **argv) {
#if defined(_WIN32)
  /* Keep stdout/stderr in binary mode so `\n` is not translated to `\r\n`,
     matching Node's output, and initialise Winsock before any extension uses
     sockets (the generated `main` calls this before running the program). */
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  xt_program_argc = argc;
  xt_program_argv = argv;
}

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
    longjmp(g_try_top->buf, 1);
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

xt_value xt_console_dir(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  return XT_UNDEFINED;
}

xt_value xt_console_trace(int32_t argc, xt_value *argv) {
  fputs("Trace", stderr);
  for (int32_t i = 0; i < argc; i++) {
    fputc(' ', stderr);
    xt_inspect(argv[i], stderr);
  }
  fputc('\n', stderr);
  return XT_UNDEFINED;
}

xt_value xt_console_assert(int32_t argc, xt_value *argv) {
  if (argc == 0 || !xt_truthy(argv[0])) {
    fputs("Assertion failed", stderr);
    if (argc > 1) fputs(":", stderr);
    for (int32_t i = 1; i < argc; i++) {
      fputc(' ', stderr);
      xt_inspect(argv[i], stderr);
    }
    fputc('\n', stderr);
  }
  return XT_UNDEFINED;
}

#define XT_CONSOLE_MAX_LABELS 32
static char *xt_console_labels[XT_CONSOLE_MAX_LABELS];
static long xt_console_counts[XT_CONSOLE_MAX_LABELS];
static clock_t xt_console_timers[XT_CONSOLE_MAX_LABELS];
static int xt_console_label_count = 0;
static int xt_console_depth = 0;

static int xt_console_find_label(const char *label) {
  for (int i = 0; i < xt_console_label_count; i++) {
    if (strcmp(xt_console_labels[i], label) == 0) return i;
  }
  if (xt_console_label_count >= XT_CONSOLE_MAX_LABELS) return -1;
  int index = xt_console_label_count++;
  xt_console_labels[index] = (char *)malloc(strlen(label) + 1);
  memcpy(xt_console_labels[index], label, strlen(label) + 1);
  xt_console_counts[index] = 0;
  xt_console_timers[index] = 0;
  return index;
}

static const char *xt_console_label(int32_t argc, xt_value *argv) {
  if (argc == 0) return "default";
  return xt_string_data(xt_to_string(argv[0]));
}

xt_value xt_console_count(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index < 0) return XT_UNDEFINED;
  xt_console_counts[index]++;
  printf("%s: %ld\n", xt_console_labels[index], xt_console_counts[index]);
  return XT_UNDEFINED;
}

xt_value xt_console_count_reset(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) xt_console_counts[index] = 0;
  return XT_UNDEFINED;
}

xt_value xt_console_group(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  xt_console_depth++;
  return XT_UNDEFINED;
}

xt_value xt_console_group_end(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_console_depth > 0) xt_console_depth--;
  return XT_UNDEFINED;
}

xt_value xt_console_table(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  return XT_UNDEFINED;
}

xt_value xt_console_time(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) xt_console_timers[index] = clock();
  return XT_UNDEFINED;
}

xt_value xt_console_time_log(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0 && xt_console_timers[index] != 0) {
    double ms = (double)(clock() - xt_console_timers[index]) * 1000.0 / CLOCKS_PER_SEC;
    printf("%s: %.3fms\n", xt_console_labels[index], ms);
  }
  return XT_UNDEFINED;
}

xt_value xt_console_time_end(int32_t argc, xt_value *argv) {
  xt_console_time_log(argc, argv);
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) xt_console_timers[index] = 0;
  return XT_UNDEFINED;
}
