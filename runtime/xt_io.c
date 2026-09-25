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
 * `setjmp` there. The C runtime (see `xt_try_setjmp`) calls whatever the
 * active `<setjmp.h>` declares — one argument on MSVC (clang injects the
 * frame) and two on MinGW-w64. `_setjmp`/`_setjmpex` are used rather than the
 * exported `setjmp` symbol, whose Windows ABI is an incompatible two-argument
 * routine; on Linux/macOS `_setjmp` takes only the buffer and pairs with
 * `longjmp` (the XSI `_longjmp` does not exist on Windows).
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
static xt_try_frame *g_try_top = NULL;

/* Program arguments, captured once by `xt_set_program_args` from `main`. */
int32_t xt_program_argc = 0;
char **xt_program_argv = NULL;

#if defined(_WIN32)
/*
 * The UCRT validates its low-level I/O arguments and, when one is out of
 * range, calls the invalid-parameter handler. The default handler terminates
 * the process through `_invoke_watson`, which fast-fails with exit status
 * 0xC0000409. That makes POSIX-style calls such as `close(9999)` abort instead
 * of reporting `EBADF`. Install a no-op handler so the CRT returns its normal
 * error code (and sets `errno`) instead of crashing.
 */
static void __cdecl xt_noop_invalid_parameter_handler(const wchar_t *expression, const wchar_t *function_name,
                                                      const wchar_t *file_name, unsigned int line_number,
                                                      uintptr_t reserved) {
  (void)expression;
  (void)function_name;
  (void)file_name;
  (void)line_number;
  (void)reserved;
}
#endif

void xt_set_program_args(int32_t argc, char **argv) {
#if defined(_WIN32)
  /* Keep stdout/stderr in binary mode so `\n` is not translated to `\r\n`,
     matching Node's output, and initialise Winsock before any extension uses
     sockets (the generated `main` calls this before running the program). */
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  _set_invalid_parameter_handler(xt_noop_invalid_parameter_handler);
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

/* Save/restore the innermost try frame. Generators use this to swap their own
 * exception stack in while they run on their private coroutine stack. */
void *xt_try_mark(void) { return (void *)g_try_top; }

void xt_try_restore(void *mark) { g_try_top = (xt_try_frame *)mark; }

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

static int xt_inspect_identifier_key(xt_string *key) {
  if (key->length == 0) return 0;
  char c = key->data[0];
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')) return 0;
  for (uint32_t i = 1; i < key->length; i++) {
    c = key->data[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$')) return 0;
  }
  return 1;
}

/* Render a string the way `util.inspect` does inside a container: quoted,
 * preferring whichever quote character needs fewer escapes. */
static void xt_inspect_string(xt_string *s, FILE *out) {
  int hasSingle = 0;
  int hasDouble = 0;
  for (uint32_t i = 0; i < s->length; i++) {
    if (s->data[i] == '\'') hasSingle = 1;
    else if (s->data[i] == '"') hasDouble = 1;
  }
  char quote = (hasSingle && !hasDouble) ? '"' : '\'';
  fputc(quote, out);
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)s->data[i];
    switch (c) {
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      case '\f': fputs("\\f", out); break;
      case '\b': fputs("\\b", out); break;
      case '\v': fputs("\\v", out); break;
      default:
        if (c == (unsigned char)quote) { fputc('\\', out); fputc(c, out); }
        else if (c < 0x20) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
  }
  fputc(quote, out);
}

static void xt_inspect(xt_value v, FILE *out, int nested) {
  if (xt_is_symbol(v)) {
    xt_string *text = xt_as_string(xt_symbol_to_string(v));
    fwrite(text->data, 1, text->length, out);
    return;
  }
  if (XT_IS_BIGINT(v)) {
    xt_value text = xt_bigint_to_decimal(v);
    xt_string *s = xt_as_string(text);
    fwrite(s->data, 1, s->length, out);
    fputc('n', out);
    return;
  }
  if (nested && XT_IS_STRING(v)) {
    xt_inspect_string(xt_as_string(v), out);
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
      xt_inspect(array->items[i], out, 1);
    }
    if (XT_IS_OBJECT(array->extra)) {
      xt_object *extra = (xt_object *)XT_GET_PTR(array->extra);
      for (uint32_t i = 0; i < extra->count; i++) {
        if (array->length > 0 || i > 0) fputs(", ", out);
        else fputc(' ', out);
        xt_value key = extra->properties[i].key;
        if (xt_is_symbol(key)) {
          xt_string *text = xt_as_string(xt_symbol_to_string(key));
          fwrite(text->data, 1, text->length, out);
        } else {
          xt_string *keyString = xt_as_string(key);
          fwrite(keyString->data, 1, keyString->length, out);
        }
        fputs(": ", out);
        xt_inspect(extra->properties[i].value, out, 1);
      }
    }
    if (array->length > 0 || (XT_IS_OBJECT(array->extra) && ((xt_object *)XT_GET_PTR(array->extra))->count > 0)) fputc(' ', out);
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
    /* `util.inspect` lists string keys first, then symbol keys, in insertion
     * order; symbol keys are printed without brackets. */
    int written = 0;
    for (int pass = 0; pass < 2; pass++) {
      for (uint32_t i = 0; i < obj->count; i++) {
        xt_value key = obj->properties[i].key;
        int isSymbol = xt_is_symbol(key);
        if ((pass == 0) == isSymbol) continue;
        if (written > 0) fputs(", ", out);
        else fputc(' ', out);
        written++;
        if (isSymbol) {
          xt_string *text = xt_as_string(xt_symbol_to_string(key));
          fwrite(text->data, 1, text->length, out);
        } else {
          xt_string *keyString = xt_as_string(key);
          if (xt_inspect_identifier_key(keyString)) fwrite(keyString->data, 1, keyString->length, out);
          else xt_inspect_string(keyString, out);
        }
        fputs(": ", out);
        xt_inspect(obj->properties[i].value, out, 1);
      }
    }
    if (written > 0) fputc(' ', out);
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
  xt_inspect(v, out, 0);
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

/* -- base64 globals (btoa/atob) ------------------------------------------- */

static const char xt_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

xt_value xt_btoa(int32_t argc, xt_value *argv) {
  const char *input = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  if (!input) input = "";
  size_t length = strlen(input);
  size_t outLength = ((length + 2) / 3) * 4;
  char *buffer = (char *)malloc(outLength + 1);
  if (!buffer) return xt_string_from_cstr("");
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    unsigned int n = ((unsigned char)input[i] << 16) | ((unsigned char)input[i + 1] << 8) |
                     (unsigned char)input[i + 2];
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    unsigned int n = (unsigned char)input[i] << 16;
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = '=';
    buffer[j++] = '=';
  } else if (length - i == 2) {
    unsigned int n = ((unsigned char)input[i] << 16) | ((unsigned char)input[i + 1] << 8);
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = '=';
  }
  buffer[j] = 0;
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

static int xt_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

xt_value xt_atob(int32_t argc, xt_value *argv) {
  const char *input = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  if (!input) input = "";
  size_t length = strlen(input);
  char *buffer = (char *)malloc(length / 4 * 3 + 1);
  if (!buffer) return xt_string_from_cstr("");
  size_t out = 0;
  int values[4];
  int count = 0;
  for (size_t i = 0; i < length; i++) {
    char c = input[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      if (c == '=') break;
      continue;
    }
    int value = xt_base64_value(c);
    if (value < 0) continue;
    values[count++] = value;
    if (count == 4) {
      buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
      buffer[out++] = (char)(((values[1] & 15) << 4) | (values[2] >> 2));
      buffer[out++] = (char)(((values[2] & 3) << 6) | values[3]);
      count = 0;
    }
  }
  if (count == 2) {
    buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
  } else if (count == 3) {
    buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
    buffer[out++] = (char)(((values[1] & 15) << 4) | (values[2] >> 2));
  }
  xt_value result = xt_string_new(buffer, out);
  free(buffer);
  return result;
}
