/*
 * `process.stdin`, `process.stdout` and `process.stderr` for xbintsc.
 *
 * Each stream is an object carrying the base EventEmitter surface from
 * `node_common.h`, so `stdout.on("resize", fn)` and `stdin.on("data", fn)`
 * behave like Node's. The write side is a direct `fputs`/`fflush`; the read side
 * has no portable readiness notification (Windows `select(2)` only understands
 * sockets), so a blocking read runs on the shared async I/O pool and its
 * completion — which executes on the main thread — decodes the bytes and emits
 * the `data` event. Raw mode flips the console/terminal into single-key bytes so
 * a TUI receives escape sequences instead of line-buffered text.
 *
 * The terminal mode is restored by an `atexit` handler: a compiled TUI that
 * exits without calling `setRawMode(false)` (a crash, `process.exit`) must not
 * leave the shell in raw mode.
 */

#include "../node_common.h"
#include "streams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define XT_STREAM_FILENO _fileno
#define XT_STREAM_ISATTY _isatty
#define XT_STREAM_READ _read
#if !defined(ENABLE_VIRTUAL_TERMINAL_INPUT)
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#else
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#define XT_STREAM_FILENO fileno
#define XT_STREAM_ISATTY isatty
#define XT_STREAM_READ read
#endif

/** Bytes requested per stdin read; also the largest chunk a `data` event sees. */
#define XT_STDIN_CHUNK 4096

static xt_value xt_stream_stdout = 0;
static xt_value xt_stream_stderr = 0;
static xt_value xt_stream_stdin = 0;

/* -- terminal geometry ---------------------------------------------------- */

static int xt_stream_is_tty(FILE *stream) {
  int fd = XT_STREAM_FILENO(stream);
  return fd >= 0 && XT_STREAM_ISATTY(fd) ? 1 : 0;
}

static int xt_stream_columns(FILE *stream) {
#if defined(_WIN32)
  (void)stream;
#endif
  const char *configured = getenv("COLUMNS");
  if (configured) {
    int columns = atoi(configured);
    if (columns > 0) return columns;
  }
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
    return (int)(info.srWindow.Right - info.srWindow.Left + 1);
  }
#else
  struct winsize size;
  if (ioctl(XT_STREAM_FILENO(stream), TIOCGWINSZ, &size) == 0 && size.ws_col > 0) return (int)size.ws_col;
#endif
  return 80;
}

static int xt_stream_rows(FILE *stream) {
#if defined(_WIN32)
  (void)stream;
#endif
  const char *configured = getenv("LINES");
  if (configured) {
    int rows = atoi(configured);
    if (rows > 0) return rows;
  }
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
    return (int)(info.srWindow.Bottom - info.srWindow.Top + 1);
  }
#else
  struct winsize size;
  if (ioctl(XT_STREAM_FILENO(stream), TIOCGWINSZ, &size) == 0 && size.ws_row > 0) return (int)size.ws_row;
#endif
  return 24;
}

/* -- output streams ------------------------------------------------------- */

static xt_value xt_stream_write(FILE *stream, int32_t argc, xt_value *argv) {
  if (argc > 0) {
    const char *text = xt_string_data(xt_to_string(argv[0]));
    if (text) fputs(text, stream);
    fflush(stream);
  }
  return xt_bool(1);
}

static xt_value xt_stream_stdout_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  return xt_stream_write(stdout, argc, argv);
}

static xt_value xt_stream_stderr_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  return xt_stream_write(stderr, argc, argv);
}

/** `end()` / `destroy()`: flushing is immediate, so ending is a no-op. */
static xt_value xt_stream_noop(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_stream_output(FILE *stream, void *write_fn, int fd) {
  xt_value object = xt_object_new();
  xt_node_install_emitter(object);
  xt_node_define_method(object, "write", write_fn);
  xt_node_define_method(object, "end", (void *)xt_stream_noop);
  xt_node_define_method(object, "destroy", (void *)xt_stream_noop);
  xt_node_set(object, "fd", xt_number((double)fd));
  xt_node_set(object, "isTTY", xt_bool(xt_stream_is_tty(stream)));
  xt_node_set(object, "columns", xt_number((double)xt_stream_columns(stream)));
  xt_node_set(object, "rows", xt_number((double)xt_stream_rows(stream)));
  xt_node_set(object, "writable", xt_bool(1));
  return object;
}

xt_value xt_process_stdout_object(void) {
  xt_gc_add_root(&xt_stream_stdout);
  if (!xt_stream_stdout) xt_stream_stdout = xt_stream_output(stdout, (void *)xt_stream_stdout_write, 1);
  return xt_stream_stdout;
}

xt_value xt_process_stderr_object(void) {
  xt_gc_add_root(&xt_stream_stderr);
  if (!xt_stream_stderr) xt_stream_stderr = xt_stream_output(stderr, (void *)xt_stream_stderr_write, 2);
  return xt_stream_stderr;
}

/* -- raw mode ------------------------------------------------------------- */

#if defined(_WIN32)
static DWORD xt_stdin_saved_mode;
static int xt_stdin_saved = 0;
#else
static struct termios xt_stdin_saved_termios;
static int xt_stdin_saved = 0;
#endif
static int xt_stdin_raw = 0;
static int xt_stream_atexit_installed = 0;

static void xt_stdin_apply_raw_mode(int enable);

static void xt_stdin_restore_terminal(void) { xt_stdin_apply_raw_mode(0); }

#if defined(_WIN32)
static void xt_stdin_apply_raw_mode(int enable) {
  HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) return;
  if (!xt_stdin_saved) {
    xt_stdin_saved_mode = mode;
    xt_stdin_saved = 1;
  }
  if (enable) {
    mode &= ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
  } else if (xt_stdin_saved) {
    mode = xt_stdin_saved_mode;
  }
  SetConsoleMode(handle, mode);
  xt_stdin_raw = enable;
}
#else
static void xt_stdin_apply_raw_mode(int enable) {
  if (!XT_STREAM_ISATTY(0)) return;
  if (!xt_stdin_saved) {
    if (tcgetattr(0, &xt_stdin_saved_termios) != 0) return;
    xt_stdin_saved = 1;
  }
  if (enable) {
    struct termios raw = xt_stdin_saved_termios;
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &raw);
  } else {
    tcsetattr(0, TCSAFLUSH, &xt_stdin_saved_termios);
  }
  xt_stdin_raw = enable;
}
#endif

/* -- stdin reader --------------------------------------------------------- */

typedef struct {
  xt_async_job job;
  char *buffer;
  int32_t length;
  int eof;
} xt_stdin_job;

static int xt_stdin_active = 0;
static int xt_stdin_in_flight = 0;
static int xt_stdin_eof = 0;

static void xt_stdin_job_release(xt_async_job *job) {
  free(((xt_stdin_job *)job)->buffer);
  free(job);
}

/* Worker thread: only plain memory and a blocking read. */
static void xt_stdin_job_run(xt_async_job *job) {
  xt_stdin_job *req = (xt_stdin_job *)job;
  int32_t received = 0;
  for (;;) {
    int got = (int)XT_STREAM_READ(0, req->buffer, (unsigned)XT_STDIN_CHUNK);
    if (got > 0) {
      received = (int32_t)got;
      break;
    }
#if !defined(_WIN32)
    if (got < 0 && errno == EINTR) continue;
#endif
    received = 0;
    break;
  }
  req->length = received;
  req->eof = received <= 0;
}

static void xt_stdin_submit(void);

/* Main thread: turn the bytes into a `data` event and queue the next read. */
static void xt_stdin_job_complete(xt_async_job *job) {
  xt_stdin_job *req = (xt_stdin_job *)job;
  xt_stdin_in_flight = 0;
  xt_value stream = xt_process_stdin_object();
  if (req->length > 0) {
    xt_value chunk = xt_string_new(req->buffer, (size_t)req->length);
    xt_node_emit(stream, "data", 1, &chunk);
  }
  if (req->eof) {
    xt_stdin_eof = 1;
    xt_stdin_active = 0;
    xt_node_emit0(stream, "end");
    xt_node_emit0(stream, "close");
    return;
  }
  if (xt_stdin_active) xt_stdin_submit();
}

static void xt_stdin_submit(void) {
  if (xt_stdin_in_flight || xt_stdin_eof) return;
  xt_stdin_job *req = (xt_stdin_job *)calloc(1, sizeof(xt_stdin_job));
  if (!req) return;
  req->buffer = (char *)malloc(XT_STDIN_CHUNK);
  if (!req->buffer) {
    free(req);
    return;
  }
  req->job.run = xt_stdin_job_run;
  req->job.complete = xt_stdin_job_complete;
  req->job.release = xt_stdin_job_release;
  /* Keep the stream (and therefore its listeners) alive while the read runs. */
  req->job.roots[0] = xt_process_stdin_object();
  xt_stdin_in_flight = 1;
  xt_async_submit(&req->job);
}

/* -- stdin methods -------------------------------------------------------- */

static xt_value xt_stdin_resume(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_stdin_active = 1;
  xt_stdin_submit();
  return thisValue;
}

static xt_value xt_stdin_pause(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_stdin_active = 0;
  return thisValue;
}

static xt_value xt_stdin_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value encoding = xt_arg(argc, argv, 0);
  xt_node_set(thisValue, "encoding", XT_IS_STRING(encoding) ? encoding : xt_string_from_cstr("utf8"));
  return thisValue;
}

static xt_value xt_stdin_set_raw_mode(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  int enable = argc > 0 && xt_truthy(argv[0]);
  if (!xt_stream_atexit_installed) {
    xt_stream_atexit_installed = 1;
    atexit(xt_stdin_restore_terminal);
  }
  xt_stdin_apply_raw_mode(enable);
  xt_node_set(thisValue, "isRaw", xt_bool(xt_stdin_raw));
  return thisValue;
}

/* `stdin.on("data", ...)` switches the stream to flowing mode, like Node. */
static xt_value xt_stdin_on(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  xt_value result = xt_node_on_impl(thisValue, env, argc, argv);
  const char *event = xt_node_cstr(xt_arg(argc, argv, 0));
  if (event && strcmp(event, "data") == 0) {
    xt_stdin_active = 1;
    xt_stdin_submit();
  }
  return result;
}

static xt_value xt_stdin_destroy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_stdin_active = 0;
  return thisValue;
}

xt_value xt_process_stdin_object(void) {
  xt_gc_add_root(&xt_stream_stdin);
  if (xt_stream_stdin) return xt_stream_stdin;
#if defined(_WIN32)
  /* Raw reads must not be CRLF-translated by the CRT. */
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  xt_value object = xt_object_new();
  xt_node_install_emitter(object);
  xt_node_define_method(object, "on", (void *)xt_stdin_on);
  xt_node_define_method(object, "addListener", (void *)xt_stdin_on);
  xt_node_define_method(object, "resume", (void *)xt_stdin_resume);
  xt_node_define_method(object, "pause", (void *)xt_stdin_pause);
  xt_node_define_method(object, "setEncoding", (void *)xt_stdin_set_encoding);
  xt_node_define_method(object, "setRawMode", (void *)xt_stdin_set_raw_mode);
  xt_node_define_method(object, "destroy", (void *)xt_stdin_destroy);
  xt_node_set(object, "fd", xt_number(0));
  xt_node_set(object, "isTTY", xt_bool(xt_stream_is_tty(stdin)));
  xt_node_set(object, "isRaw", xt_bool(0));
  xt_node_set(object, "readable", xt_bool(1));
  xt_node_set(object, "encoding", xt_string_from_cstr("utf8"));
  xt_stream_stdin = object;
  return xt_stream_stdin;
}
