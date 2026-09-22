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

/*
 * `import.meta` support. The compiler resolves module URLs at build time, but
 * a self-hosted binary only knows where *it* lives, so `import.meta.url` is the
 * executable's own `file://` URL (and `dirname`/`filename` its path parts).
 * This lets the compiler locate an adjacent `runtime/` directory at run time.
 */
static xt_value xt_file_url(const char *path) {
  char url[4300];
  size_t j = 0;
  const char *prefix = "file://";
  for (; *prefix; prefix++) url[j++] = *prefix;
#if defined(_WIN32)
  url[j++] = '/';
#endif
  for (const char *p = path; *p && j < sizeof(url) - 1; p++) url[j++] = (*p == '\\') ? '/' : *p;
  url[j] = 0;
  return xt_string_from_cstr(url);
}

xt_value xt_import_meta(xt_value name) {
  const char *prop = xt_string_data(name);
  const char *raw = (xt_program_argv && xt_program_argv[0]) ? xt_program_argv[0] : "";
  char resolved[4096];
  resolved[0] = 0;
  if (raw[0]) {
#if defined(_WIN32)
    if (_fullpath(resolved, raw, sizeof(resolved)) == NULL) resolved[0] = 0;
#else
    if (realpath(raw, resolved) == NULL) resolved[0] = 0;
#endif
  }
  const char *abs = resolved[0] ? resolved : raw;
  if (prop && strcmp(prop, "dirname") == 0) {
    char dir[4096];
    size_t n = strlen(abs);
    if (n >= sizeof(dir)) n = sizeof(dir) - 1;
    memcpy(dir, abs, n);
    dir[n] = 0;
    char *slash = strrchr(dir, '/');
#if defined(_WIN32)
    char *back = strrchr(dir, '\\');
    if (back && (!slash || back > slash)) slash = back;
#endif
    if (slash) {
      if (slash == dir) slash[1] = 0;
      else *slash = 0;
    }
    return xt_string_from_cstr(dir);
  }
  if (prop && strcmp(prop, "filename") == 0) return xt_string_from_cstr(abs);
  return xt_file_url(abs);
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
  if (frame && g_try_top == frame) {
    g_try_top = frame->prev;
    free(frame);
  }
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
 *
 * Recursion tracks the ancestor chain so self-referential containers print
 * Node's `[Circular *N]` marker instead of recursing forever. `N` is the
 * 1-based position of the repeated container in the current path.
 */
#define XT_INSPECT_MAX_DEPTH 512
static const void *xt_inspect_stack[XT_INSPECT_MAX_DEPTH];
static int xt_inspect_depth = 0;

static int xt_inspect_seen(const void *ptr) {
  for (int i = 0; i < xt_inspect_depth; i++) {
    if (xt_inspect_stack[i] == ptr) return i + 1;
  }
  return 0;
}

static void xt_inspect_push(const void *ptr) {
  if (xt_inspect_depth < XT_INSPECT_MAX_DEPTH) xt_inspect_stack[xt_inspect_depth++] = ptr;
}

static void xt_inspect_pop(void) {
  if (xt_inspect_depth > 0) xt_inspect_depth--;
}

static void xt_inspect(xt_value v, FILE *out) {
  if (XT_IS_BIGINT(v)) {
    xt_value text = xt_bigint_to_decimal(v);
    xt_string *s = xt_as_string(text);
    fwrite(s->data, 1, s->length, out);
    fputc('n', out);
    return;
  }
  if (XT_IS_ARRAY(v)) {
    xt_array *array = (xt_array *)XT_GET_PTR(v);
    int seen = xt_inspect_seen(array);
    if (seen > 0) {
      fprintf(out, "[Circular *%d]", seen);
      return;
    }
    if (xt_inspect_depth >= XT_INSPECT_MAX_DEPTH) {
      fputs("[Array]", out);
      return;
    }
    xt_inspect_push(array);
    fputc('[', out);
    for (uint32_t i = 0; i < array->length; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      xt_inspect(array->items[i], out);
    }
    if (array->length > 0) fputc(' ', out);
    fputc(']', out);
    xt_inspect_pop();
    return;
  }
  if (XT_IS_OBJECT(v)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(v);
    int seen = xt_inspect_seen(obj);
    if (seen > 0) {
      fprintf(out, "[Circular *%d]", seen);
      return;
    }
    if (xt_inspect_depth >= XT_INSPECT_MAX_DEPTH) {
      fputs("[Object]", out);
      return;
    }
    xt_inspect_push(obj);
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
    xt_inspect_pop();
    return;
  }
  xt_print_value(v, out);
}

/* Inspect one top-level value; reset the ancestor chain so nested console
 * calls (or a `longjmp` out of a throw) never leave stale state behind. */
static void xt_inspect_top(xt_value v, FILE *out) {
  xt_inspect_depth = 0;
  xt_inspect(v, out);
}

void xt_console_log(int32_t argc, xt_value *argv) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', stdout);
    xt_inspect_top(argv[i], stdout);
  }
  fputc('\n', stdout);
}

static void xt_console_write(int32_t argc, xt_value *argv, FILE *out) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', out);
    xt_inspect_top(argv[i], out);
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
    xt_inspect_top(argv[i], stderr);
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
      xt_inspect_top(argv[i], stderr);
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
