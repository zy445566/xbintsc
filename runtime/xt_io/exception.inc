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
