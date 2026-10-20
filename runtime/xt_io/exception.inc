/* ------------------------------------------------------------------------- */
/* Exceptions                                                                */
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

/* ------------------------------------------------------------------------- */
/* Shadow call stack                                                         */
/* ------------------------------------------------------------------------- */

/*
 * Compiled functions push one frame on entry and pop it before returning, so
 * `Error` can capture a JavaScript-level stack without any debug metadata.
 * Generator bodies are not tracked (they switch stacks and can be suspended
 * indefinitely); a normal function called from a generator is tracked normally.
 *
 * `name`/`file` point into the module's static string constants; the runtime
 * never frees them. The depth is trimmed when a `try` frame is left, so frames
 * from calls unwound by a throw cannot leak into a later stack.
 */
#define XT_STACK_MAX_DEPTH 256

typedef struct {
  const char *name;
  const char *file;
  int32_t line;
} xt_stack_entry;

static xt_stack_entry g_stack[XT_STACK_MAX_DEPTH];
static int32_t g_stack_depth = 0;

void xt_stack_push(const char *name, const char *file, int32_t line) {
  if (g_stack_depth >= XT_STACK_MAX_DEPTH) return;
  xt_stack_entry *entry = &g_stack[g_stack_depth++];
  entry->name = name;
  entry->file = file;
  entry->line = line;
}

void xt_stack_pop(void) {
  if (g_stack_depth > 0) g_stack_depth--;
}

int32_t xt_stack_depth(void) { return g_stack_depth; }

void xt_stack_set_line(int32_t line) {
  /* Called just before an `Error` is constructed, so the innermost frame
   * reports the construction line rather than the function's declaration. */
  if (g_stack_depth > 0) g_stack[g_stack_depth - 1].line = line;
}

void xt_stack_trim(int32_t depth) {
  if (depth < 0) depth = 0;
  if (depth < g_stack_depth) g_stack_depth = depth;
}

static void xt_stack_append(char **buffer, size_t *length, size_t *capacity, const char *text) {
  size_t count = strlen(text);
  if (*length + count + 1 > *capacity) {
    size_t next = *capacity ? *capacity : 256;
    while (next < *length + count + 1) next *= 2;
    char *grown = (char *)realloc(*buffer, next);
    if (!grown) return;
    *buffer = grown;
    *capacity = next;
  }
  memcpy(*buffer + *length, text, count);
  *length += count;
  (*buffer)[*length] = 0;
}

xt_value xt_stack_capture(const char *name, const char *message) {
  const char *header = name && name[0] ? name : "Error";
  char *buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  xt_stack_append(&buffer, &length, &capacity, header);
  if (message && message[0]) {
    xt_stack_append(&buffer, &length, &capacity, ": ");
    xt_stack_append(&buffer, &length, &capacity, message);
  }
  xt_stack_append(&buffer, &length, &capacity, "\n");
  for (int32_t index = g_stack_depth - 1; index >= 0; index--) {
    const xt_stack_entry *entry = &g_stack[index];
    char line[24];
    xt_stack_append(&buffer, &length, &capacity, "    at ");
    xt_stack_append(&buffer, &length, &capacity, entry->name && entry->name[0] ? entry->name : "anonymous");
    xt_stack_append(&buffer, &length, &capacity, " (");
    xt_stack_append(&buffer, &length, &capacity, entry->file && entry->file[0] ? entry->file : "<anonymous>");
    if (entry->line > 0) {
      snprintf(line, sizeof(line), ":%d", (int)entry->line);
      xt_stack_append(&buffer, &length, &capacity, line);
    }
    xt_stack_append(&buffer, &length, &capacity, ")\n");
  }
  xt_value result = xt_string_new(buffer ? buffer : "", length);
  free(buffer);
  return result;
}

xt_value xt_error_stack(xt_value error) {
  if (!XT_IS_OBJECT(error)) return XT_UNDEFINED;
  xt_object *object = (xt_object *)XT_GET_PTR(error);
  if (object->header.kind != XT_OBJECT_KIND_ERROR) return XT_UNDEFINED;
  return object->error_stack;
}

/* ------------------------------------------------------------------------- */
/* Fatal-signal diagnostics                                                  */
/* ------------------------------------------------------------------------- */

static char g_crash_report[16384];

static int xt_format_stack(char *out, size_t size) {
  int written = 0;
  for (int32_t index = g_stack_depth - 1; index >= 0 && written < (int)size; index--) {
    const xt_stack_entry *entry = &g_stack[index];
    int count = snprintf(out + written, size - (size_t)written, "    at %s (%s:%d)\n",
                         entry->name && entry->name[0] ? entry->name : "anonymous",
                         entry->file && entry->file[0] ? entry->file : "<anonymous>", (int)entry->line);
    if (count < 0) break;
    written += count;
  }
  return written < (int)size ? written : (int)size - 1;
}

#if defined(_WIN32)

static LONG WINAPI xt_fatal_exception(EXCEPTION_POINTERS *info) {
  unsigned long code =
      info && info->ExceptionRecord ? (unsigned long)info->ExceptionRecord->ExceptionCode : 0UL;
  int written = snprintf(g_crash_report, sizeof(g_crash_report), "\nxbintsc: fatal exception 0x%08lx\n", code);
  written += xt_format_stack(g_crash_report + written, sizeof(g_crash_report) - (size_t)written);
  _write(2, g_crash_report, (unsigned int)written);
  _exit(1);
  return EXCEPTION_EXECUTE_HANDLER;
}

void xt_install_crash_handler(void) { SetUnhandledExceptionFilter(xt_fatal_exception); }

#else

static void xt_fatal_signal(int sig) {
  int written = snprintf(g_crash_report, sizeof(g_crash_report), "\nxbintsc: fatal signal %d\n", sig);
  written += xt_format_stack(g_crash_report + written, sizeof(g_crash_report) - (size_t)written);
  ssize_t ignored = write(STDERR_FILENO, g_crash_report, (size_t)written);
  (void)ignored;
  _exit(128 + sig);
}

void xt_install_crash_handler(void) {
  signal(SIGSEGV, xt_fatal_signal);
  signal(SIGABRT, xt_fatal_signal);
  signal(SIGFPE, xt_fatal_signal);
  signal(SIGBUS, xt_fatal_signal);
#ifdef SIGILL
  signal(SIGILL, xt_fatal_signal);
#endif
}

#endif

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
  frame->stack_depth = g_stack_depth;
  g_try_top = frame;
  return (void *)frame;
}

xt_value xt_try_exception(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  if (!frame) return XT_UNDEFINED;
  /* A throw longjmps past the calls it unwinds; drop their shadow frames. */
  xt_stack_trim(frame->stack_depth);
  return frame->exception;
}

void xt_try_leave(void *framePtr) {
  xt_try_frame *frame = (xt_try_frame *)framePtr;
  if (frame && g_try_top == frame) {
    xt_stack_trim(frame->stack_depth);
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
  xt_value stack = xt_error_stack(v);
  if (stack != XT_UNDEFINED) {
    xt_string *text = xt_as_string(stack);
    if (text) {
      fprintf(stderr, "Uncaught %s\n", text->data);
      exit(1);
    }
  }
  xt_value message = xt_to_string(v);
  xt_string *s = xt_as_string(message);
  fprintf(stderr, "Uncaught %s\n", s->data);
  exit(1);
}
