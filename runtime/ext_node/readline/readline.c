/*
 * Node.js `readline` for xbintsc.
 *
 * A line-oriented reader over the process's standard input. Readline is
 * inherently asynchronous (it waits for the user while other timers keep
 * running), so the blocking byte read runs on the runtime's async I/O pool and
 * its completion delivers `line`/`close` events on the main thread. One read is
 * in flight per interface; `pause()` buffers a line that arrives while paused
 * and `resume()` drains the buffer before reading again.
 *
 * Supported: `createInterface`, `Interface`, `question` (queued, one answer per
 * call), `on`/`once`/`off`/`removeListener`/`removeAllListeners` for
 * `line`/`close`/`pause`/`resume`, `prompt`/`setPrompt`/`getPrompt`, `close`,
 * `pause`, `resume`, `write`, `setRawMode`, and the cursor helpers `cursorTo`,
 * `moveCursor`, `clearLine`, `clearScreenDown`, `emitKeypressEvents`.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define xt_readline_read _read
#else
#include <unistd.h>
#define xt_readline_read read
#endif

#define XT_READLINE_MAX_INTERFACES 16

/* One rooted slot per live interface. The object owns its listener arrays and
 * pending questions, so marking the object is enough. A listener callback can
 * allocate (and therefore collect), so listener snapshots are rooted too; four
 * nested dispatches are enough for any realistic callback chain. */
#define XT_READLINE_EMIT_DEPTH 4

static xt_value g_readline_interfaces[XT_READLINE_MAX_INTERFACES];
static xt_value g_readline_emit_roots[XT_READLINE_MAX_INTERFACES][XT_READLINE_EMIT_DEPTH];
static int g_readline_roots_registered = 0;

typedef struct {
  xt_async_job base;  /* must stay first: the pool casts to this */
  int slot;
  char *data;
  size_t length;
  size_t capacity;
  int eof;
} xt_readline_job;

static void xt_readline_scan_roots(void) {
  for (int i = 0; i < XT_READLINE_MAX_INTERFACES; i++) {
    xt_gc_mark_value(g_readline_interfaces[i]);
    for (int depth = 0; depth < XT_READLINE_EMIT_DEPTH; depth++) {
      xt_gc_mark_value(g_readline_emit_roots[i][depth]);
    }
  }
}

static void xt_readline_ensure_roots(void) {
  if (g_readline_roots_registered) return;
  g_readline_roots_registered = 1;
  for (int i = 0; i < XT_READLINE_MAX_INTERFACES; i++) {
    g_readline_interfaces[i] = XT_UNDEFINED;
    for (int depth = 0; depth < XT_READLINE_EMIT_DEPTH; depth++) {
      g_readline_emit_roots[i][depth] = XT_UNDEFINED;
    }
  }
  xt_gc_register_root_provider(xt_readline_scan_roots);
}

/* -- small value helpers -------------------------------------------------- */

static int xt_readline_truthy(xt_value object, const char *key) {
  return xt_truthy(xt_object_get_cstr(object, key));
}

static void xt_readline_set_bool(xt_value object, const char *key, int value) {
  xt_object_set(object, xt_string_from_cstr(key), value ? XT_TRUE : XT_FALSE);
}

static int xt_readline_slot(xt_value self) {
  xt_value slot = xt_object_get_cstr(self, "__xt_rl_slot");
  if (!XT_IS_NUMBER(slot)) return -1;
  int index = (int)xt_to_number(slot);
  if (index < 0 || index >= XT_READLINE_MAX_INTERFACES) return -1;
  return index;
}

static void xt_readline_write(const char *data, size_t length) {
  if (data == NULL || length == 0) return;
  fwrite(data, 1, length, stdout);
  fflush(stdout);
}

/* -- listeners ------------------------------------------------------------ */

static const char *xt_readline_listener_key(const char *type) {
  if (strcmp(type, "line") == 0) return "__xt_rl_line";
  if (strcmp(type, "close") == 0) return "__xt_rl_close";
  if (strcmp(type, "pause") == 0) return "__xt_rl_pause";
  if (strcmp(type, "resume") == 0) return "__xt_rl_resume";
  return NULL;
}

static void xt_readline_add_listener(xt_value self, const char *type, xt_value fn, int once) {
  if (!XT_IS_FUNCTION(fn)) return;
  const char *key = xt_readline_listener_key(type);
  if (key == NULL) return;
  xt_value list = xt_object_get_cstr(self, key);
  if (!XT_IS_ARRAY(list)) {
    list = xt_array_new(0, NULL);
    xt_object_set(self, xt_string_from_cstr(key), list);
  }
  xt_value entry = xt_object_new();
  xt_object_set(entry, xt_string_from_cstr("fn"), fn);
  xt_object_set(entry, xt_string_from_cstr("once"), once ? XT_TRUE : XT_FALSE);
  xt_array_push(list, entry);
}

static void xt_readline_remove_listener(xt_value self, const char *type, xt_value fn) {
  const char *key = xt_readline_listener_key(type);
  if (key == NULL) return;
  xt_value list = xt_object_get_cstr(self, key);
  if (!XT_IS_ARRAY(list)) return;
  xt_value kept = xt_array_new(0, NULL);
  int32_t count = xt_array_size(list);
  for (int32_t i = 0; i < count; i++) {
    xt_value entry = xt_array_get(list, xt_number((double)i));
    xt_value handler = xt_object_get_cstr(entry, "fn");
    if (!xt_truthy(xt_seq(handler, fn))) xt_array_push(kept, entry);
  }
  xt_object_set(self, xt_string_from_cstr(key), kept);
}

static void xt_readline_emit(xt_value self, const char *type, int32_t argc, xt_value *argv) {
  const char *key = xt_readline_listener_key(type);
  if (key == NULL) return;
  xt_value list = xt_object_get_cstr(self, key);
  if (!XT_IS_ARRAY(list)) return;
  /* Snapshot the entries: a listener may remove itself or another listener.
   * The callback can allocate, so the snapshot must be a GC root for the
   * duration of the dispatch. */
  int slot = xt_readline_slot(self);
  xt_value *root = NULL;
  if (slot >= 0) {
    for (int depth = 0; depth < XT_READLINE_EMIT_DEPTH; depth++) {
      if (!XT_IS_OBJECT(g_readline_emit_roots[slot][depth])) {
        root = &g_readline_emit_roots[slot][depth];
        break;
      }
    }
  }
  int32_t count = xt_array_size(list);
  xt_value snapshot = xt_array_new(0, NULL);
  for (int32_t i = 0; i < count; i++) xt_array_push(snapshot, xt_array_get(list, xt_number((double)i)));
  if (root != NULL) *root = snapshot;
  for (int32_t i = 0; i < count; i++) {
    xt_value entry = xt_array_get(snapshot, xt_number((double)i));
    xt_value fn = xt_object_get_cstr(entry, "fn");
    if (!XT_IS_FUNCTION(fn)) continue;
    if (xt_truthy(xt_object_get_cstr(entry, "once"))) {
      xt_readline_remove_listener(self, type, fn);
    }
    xt_call_with_this(fn, self, argc, argv);
  }
  if (root != NULL) *root = XT_UNDEFINED;
}

/* -- reading -------------------------------------------------------------- */

static void xt_readline_start_read(int slot);
static void xt_readline_close(xt_value self);
static void xt_readline_deliver(xt_value self, xt_value line);
static void xt_readline_drain_pending(xt_value self);

static void xt_readline_job_run(xt_async_job *base) {
  xt_readline_job *job = (xt_readline_job *)base;
  for (;;) {
    char byte = 0;
    int read = xt_readline_read(0, &byte, 1);
    if (read <= 0) {
      /* A bare EOF reports the last line (without a newline) and then closes;
       * a read error is treated the same way so the program cannot hang. */
      job->eof = 1;
      break;
    }
    if (byte == '\n') break;
    if (byte == '\r') continue;
    if (job->length + 1 >= job->capacity) {
      size_t next = job->capacity == 0 ? 64 : job->capacity * 2;
      char *grown = (char *)realloc(job->data, next);
      if (grown == NULL) {
        job->eof = 1;
        break;
      }
      job->data = grown;
      job->capacity = next;
    }
    job->data[job->length++] = byte;
  }
  if (job->data == NULL) {
    job->data = (char *)malloc(1);
    if (job->data != NULL) job->data[0] = '\0';
  } else {
    job->data[job->length] = '\0';
  }
}

static void xt_readline_job_release(xt_async_job *base) {
  xt_readline_job *job = (xt_readline_job *)base;
  free(job->data);
  free(job);
}

static void xt_readline_job_complete(xt_async_job *base) {
  xt_readline_job *job = (xt_readline_job *)base;
  int slot = job->slot;
  xt_value self = (slot >= 0 && slot < XT_READLINE_MAX_INTERFACES)
                      ? g_readline_interfaces[slot]
                      : XT_UNDEFINED;
  /* A closed interface frees its slot; a new interface may reuse it before
   * this job completes. Deliver only to the interface that started the read. */
  if (!XT_IS_OBJECT(self) || !xt_truthy(xt_seq(self, job->base.roots[0]))) return;
  xt_readline_set_bool(self, "__xt_rl_reading", 0);
  if (xt_readline_truthy(self, "__xt_rl_closed")) return;

  xt_value line = xt_string_new(job->data != NULL ? job->data : "", job->length);
  /* EOF without any pending bytes is not a line; a final unterminated line is. */
  if (!job->eof || job->length > 0) {
    if (xt_readline_truthy(self, "__xt_rl_paused")) {
      xt_value pending = xt_object_get_cstr(self, "__xt_rl_pending");
      if (!XT_IS_ARRAY(pending)) {
        pending = xt_array_new(0, NULL);
        xt_object_set(self, xt_string_from_cstr("__xt_rl_pending"), pending);
      }
      xt_array_push(pending, line);
    } else {
      xt_readline_deliver(self, line);
    }
  }

  if (job->eof) {
    xt_readline_close(self);
    return;
  }
  if (!xt_readline_truthy(self, "__xt_rl_closed") &&
      !xt_readline_truthy(self, "__xt_rl_paused")) {
    xt_readline_start_read(slot);
  }
}

static void xt_readline_start_read(int slot) {
  if (slot < 0 || slot >= XT_READLINE_MAX_INTERFACES) return;
  xt_value self = g_readline_interfaces[slot];
  if (!XT_IS_OBJECT(self)) return;
  if (xt_readline_truthy(self, "__xt_rl_closed") || xt_readline_truthy(self, "__xt_rl_paused")) return;
  if (xt_readline_truthy(self, "__xt_rl_reading")) return;

  xt_readline_job *job = (xt_readline_job *)calloc(1, sizeof(xt_readline_job));
  if (job == NULL) return;
  job->slot = slot;
  job->base.run = xt_readline_job_run;
  job->base.complete = xt_readline_job_complete;
  job->base.release = xt_readline_job_release;
  job->base.roots[0] = self;
  xt_readline_set_bool(self, "__xt_rl_reading", 1);
  xt_async_submit(&job->base);
}

/* -- delivery ------------------------------------------------------------- */

static void xt_readline_deliver(xt_value self, xt_value line) {
  xt_value questions = xt_object_get_cstr(self, "__xt_rl_questions");
  if (XT_IS_ARRAY(questions) && xt_array_size(questions) > 0) {
    xt_value entry = xt_array_get(questions, xt_number(0));
    xt_value rest = xt_array_new(0, NULL);
    int32_t count = xt_array_size(questions);
    for (int32_t i = 1; i < count; i++) xt_array_push(rest, xt_array_get(questions, xt_number((double)i)));
    xt_object_set(self, xt_string_from_cstr("__xt_rl_questions"), rest);
    xt_value callback = xt_object_get_cstr(entry, "callback");
    if (XT_IS_FUNCTION(callback)) {
      xt_value args[1] = {line};
      xt_call_with_this(callback, XT_UNDEFINED, 1, args);
    }
    return;
  }
  xt_value args[1] = {line};
  xt_readline_emit(self, "line", 1, args);
}

static void xt_readline_drain_pending(xt_value self) {
  xt_value pending = xt_object_get_cstr(self, "__xt_rl_pending");
  if (!XT_IS_ARRAY(pending) || xt_array_size(pending) == 0) return;
  xt_object_set(self, xt_string_from_cstr("__xt_rl_pending"), xt_array_new(0, NULL));
  int32_t count = xt_array_size(pending);
  for (int32_t i = 0; i < count; i++) {
    if (xt_readline_truthy(self, "__xt_rl_closed") || xt_readline_truthy(self, "__xt_rl_paused")) break;
    xt_readline_deliver(self, xt_array_get(pending, xt_number((double)i)));
  }
}

static void xt_readline_close(xt_value self) {
  if (xt_readline_truthy(self, "__xt_rl_closed")) return;
  xt_readline_set_bool(self, "__xt_rl_closed", 1);
  xt_readline_emit(self, "close", 0, NULL);
  int slot = xt_readline_slot(self);
  if (slot >= 0 && slot < XT_READLINE_MAX_INTERFACES) g_readline_interfaces[slot] = XT_UNDEFINED;
}

/* -- Interface methods ---------------------------------------------------- */

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(rl_on) {
  (void)env;
  if (argc >= 2) {
    const char *type = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
    if (type != NULL) {
      xt_readline_add_listener(self, type, xt_arg(argc, argv, 1), 0);
      if (strcmp(type, "line") == 0) xt_readline_start_read(xt_readline_slot(self));
    }
  }
  return self;
}

METHOD(rl_once) {
  (void)env;
  if (argc >= 2) {
    const char *type = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
    if (type != NULL) {
      xt_readline_add_listener(self, type, xt_arg(argc, argv, 1), 1);
      if (strcmp(type, "line") == 0) xt_readline_start_read(xt_readline_slot(self));
    }
  }
  return self;
}

METHOD(rl_off) {
  (void)env;
  if (argc >= 2) {
    const char *type = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
    if (type != NULL) xt_readline_remove_listener(self, type, xt_arg(argc, argv, 1));
  }
  return self;
}

METHOD(rl_remove_all_listeners) {
  (void)env;
  if (argc == 0) {
    xt_object_set(self, xt_string_from_cstr("__xt_rl_line"), xt_array_new(0, NULL));
    xt_object_set(self, xt_string_from_cstr("__xt_rl_close"), xt_array_new(0, NULL));
    return self;
  }
  const char *key = xt_readline_listener_key(xt_string_data(xt_to_string(xt_arg(argc, argv, 0))));
  if (key != NULL) xt_object_set(self, xt_string_from_cstr(key), xt_array_new(0, NULL));
  return self;
}

METHOD(rl_question) {
  (void)env;
  const char *query = argc >= 1 ? xt_string_data(xt_to_string(xt_arg(argc, argv, 0))) : "";
  if (query != NULL) xt_readline_write(query, strlen(query));
  if (argc >= 2 && XT_IS_FUNCTION(xt_arg(argc, argv, 1))) {
    xt_value questions = xt_object_get_cstr(self, "__xt_rl_questions");
    if (!XT_IS_ARRAY(questions)) {
      questions = xt_array_new(0, NULL);
      xt_object_set(self, xt_string_from_cstr("__xt_rl_questions"), questions);
    }
    xt_value entry = xt_object_new();
    xt_object_set(entry, xt_string_from_cstr("callback"), xt_arg(argc, argv, 1));
    xt_array_push(questions, entry);
  }
  xt_readline_start_read(xt_readline_slot(self));
  return XT_UNDEFINED;
}

METHOD(rl_prompt) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value prompt = xt_object_get_cstr(self, "__xt_rl_prompt");
  const char *text = XT_IS_STRING(prompt) ? xt_string_data(prompt) : "> ";
  if (text != NULL) xt_readline_write(text, strlen(text));
  return XT_UNDEFINED;
}

METHOD(rl_set_prompt) {
  (void)env;
  if (argc >= 1) {
    xt_value text = xt_to_string(xt_arg(argc, argv, 0));
    xt_object_set(self, xt_string_from_cstr("__xt_rl_prompt"), text);
  }
  return XT_UNDEFINED;
}

METHOD(rl_get_prompt) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value prompt = xt_object_get_cstr(self, "__xt_rl_prompt");
  return XT_IS_STRING(prompt) ? prompt : xt_string_from_cstr("> ");
}

METHOD(rl_close) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_readline_close(self);
  return XT_UNDEFINED;
}

METHOD(rl_pause) {
  (void)env;
  (void)argc;
  (void)argv;
  if (xt_readline_truthy(self, "__xt_rl_closed")) return XT_UNDEFINED;
  xt_readline_set_bool(self, "__xt_rl_paused", 1);
  xt_readline_emit(self, "pause", 0, NULL);
  return XT_UNDEFINED;
}

METHOD(rl_resume) {
  (void)env;
  (void)argc;
  (void)argv;
  if (xt_readline_truthy(self, "__xt_rl_closed")) return XT_UNDEFINED;
  xt_readline_set_bool(self, "__xt_rl_paused", 0);
  xt_readline_emit(self, "resume", 0, NULL);
  xt_readline_drain_pending(self);
  xt_readline_start_read(xt_readline_slot(self));
  return XT_UNDEFINED;
}

METHOD(rl_write) {
  (void)env;
  (void)self;
  if (argc >= 1) {
    xt_value text = xt_to_string(xt_arg(argc, argv, 0));
    const char *data = xt_string_data(text);
    if (data != NULL) xt_readline_write(data, strlen(data));
  }
  return XT_TRUE;
}

METHOD(rl_set_raw_mode) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  /* The host has no terminal raw mode; accepted so a REPL-style program runs. */
  return XT_UNDEFINED;
}

#undef METHOD

static xt_value xt_readline_interface_new(int32_t argc, xt_value *argv) {
  xt_readline_ensure_roots();
  int slot = -1;
  for (int i = 0; i < XT_READLINE_MAX_INTERFACES; i++) {
    if (!XT_IS_OBJECT(g_readline_interfaces[i])) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return XT_UNDEFINED;

  xt_value self = xt_object_new();
  g_readline_interfaces[slot] = self;
  xt_object_set(self, xt_string_from_cstr("__xt_rl_slot"), xt_number((double)slot));
  xt_readline_set_bool(self, "__xt_rl_closed", 0);
  xt_readline_set_bool(self, "__xt_rl_paused", 0);
  xt_readline_set_bool(self, "__xt_rl_reading", 0);
  xt_object_set(self, xt_string_from_cstr("__xt_rl_line"), xt_array_new(0, NULL));
  xt_object_set(self, xt_string_from_cstr("__xt_rl_close"), xt_array_new(0, NULL));
  xt_object_set(self, xt_string_from_cstr("__xt_rl_questions"), xt_array_new(0, NULL));
  xt_object_set(self, xt_string_from_cstr("__xt_rl_pending"), xt_array_new(0, NULL));

  xt_value options = xt_arg(argc, argv, 0);
  if (XT_IS_OBJECT(options)) {
    xt_value prompt = xt_object_get_cstr(options, "prompt");
    if (XT_IS_STRING(prompt)) xt_object_set(self, xt_string_from_cstr("__xt_rl_prompt"), prompt);
  } else if (XT_IS_STRING(options)) {
    xt_object_set(self, xt_string_from_cstr("__xt_rl_prompt"), options);
  }

  xt_object_set(self, xt_string_from_cstr("on"), xt_closure_new((void *)rl_on, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("once"), xt_closure_new((void *)rl_once, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("off"), xt_closure_new((void *)rl_off, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("removeListener"),
                xt_closure_new((void *)rl_off, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("removeAllListeners"),
                xt_closure_new((void *)rl_remove_all_listeners, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("question"), xt_closure_new((void *)rl_question, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("prompt"), xt_closure_new((void *)rl_prompt, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("setPrompt"), xt_closure_new((void *)rl_set_prompt, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("getPrompt"), xt_closure_new((void *)rl_get_prompt, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("close"), xt_closure_new((void *)rl_close, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("pause"), xt_closure_new((void *)rl_pause, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("resume"), xt_closure_new((void *)rl_resume, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("write"), xt_closure_new((void *)rl_write, 0, NULL));
  xt_object_set(self, xt_string_from_cstr("setRawMode"), xt_closure_new((void *)rl_set_raw_mode, 0, NULL));
  return self;
}

/* -- module entry points -------------------------------------------------- */

xt_value xt_readline_create_interface(int32_t argc, xt_value *argv) {
  return xt_readline_interface_new(argc, argv);
}

xt_value xt_readline_interface_ctor(int32_t argc, xt_value *argv) {
  return xt_readline_interface_new(argc, argv);
}

static void xt_readline_write_escape(const char *format, int a, int b) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), format, a, b);
  xt_readline_write(buffer, strlen(buffer));
}

xt_value xt_readline_cursor_to(int32_t argc, xt_value *argv) {
  if (argc < 2) return XT_UNDEFINED;
  int x = (int)xt_to_number(xt_arg(argc, argv, 1));
  if (x < 0) x = 0;
  if (argc >= 3 && !XT_IS_UNDEFINED(xt_arg(argc, argv, 2))) {
    int y = (int)xt_to_number(xt_arg(argc, argv, 2));
    if (y < 0) y = 0;
    xt_readline_write_escape("\x1b[%d;%dH", y + 1, x + 1);
  } else {
    xt_readline_write_escape("\x1b[%dG", x + 1, 0);
  }
  return XT_UNDEFINED;
}

xt_value xt_readline_move_cursor(int32_t argc, xt_value *argv) {
  if (argc < 3) return XT_UNDEFINED;
  int dx = (int)xt_to_number(xt_arg(argc, argv, 1));
  int dy = (int)xt_to_number(xt_arg(argc, argv, 2));
  if (dy < 0) xt_readline_write_escape("\x1b[%dA", -dy, 0);
  else if (dy > 0) xt_readline_write_escape("\x1b[%dB", dy, 0);
  if (dx < 0) xt_readline_write_escape("\x1b[%dD", -dx, 0);
  else if (dx > 0) xt_readline_write_escape("\x1b[%dC", dx, 0);
  return XT_UNDEFINED;
}

xt_value xt_readline_clear_line(int32_t argc, xt_value *argv) {
  int direction = argc >= 2 ? (int)xt_to_number(xt_arg(argc, argv, 1)) : 0;
  /* Node: -1 clears to the left, 1 to the right, 0 the whole line. */
  if (direction < 0) xt_readline_write("\x1b[1K", 4);
  else if (direction > 0) xt_readline_write("\x1b[0K", 4);
  else xt_readline_write("\x1b[2K", 4);
  return XT_UNDEFINED;
}

xt_value xt_readline_clear_screen_down(int32_t argc, xt_value *argv) {
  (void)argv;
  (void)argc;
  xt_readline_write("\x1b[0J", 4);
  return XT_UNDEFINED;
}

xt_value xt_readline_emit_keypress_events(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  /* Keypress events are not synthesised; accepted for API compatibility. */
  return XT_UNDEFINED;
}
