/*
 * xbintsc runtime — worker transport core (shared by both worker APIs).
 *
 * This file owns:
 *
 *   - the EventEmitter every worker object uses (`on`/`once`/`off` plus the
 *     pending-event buffer that lets a message arrive before the program has
 *     attached its handler),
 *   - the `workerData` / worker name handoff (environment -> value),
 *   - entry-point validation: a compiled binary can only run *itself* as a
 *     worker, so `new Worker(url)` accepts the executable's own path,
 *   - the worker side of the control channel (`self` / `parentPort`).
 *
 * The parent-side socket machinery lives in `worker_transport.c`, the wire
 * format in `worker_codec.c`, and every OS difference in `worker_platform.c`.
 */

#include "worker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

xt_wrk_link *xt_wrk_parents = NULL;
xt_wrk_link *xt_wrk_scope_channel = NULL;
uint64_t xt_wrk_poll_timer = 0;
int xt_wrk_thread_counter = 1;

static xt_value xt_wrk_data_cache = XT_UNDEFINED;
static int xt_wrk_gc_registered = 0;
static char *xt_wrk_program_path_cache = NULL;

/* -- small helpers -------------------------------------------------------- */

static xt_value xt_wrk_string(const char *text) { return xt_string_from_cstr(text); }

static void xt_wrk_set(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_wrk_string(key), value);
}

static xt_value xt_wrk_get(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

/*
 * `xt_loop_add` scans its userdata as a value slot. The transport needs the link
 * pointer, and a raw pointer's tag bits are 0x0000, which no value tag uses, so
 * the collector skips it exactly like the imported-function pointers the
 * compiler stores in closure environments.
 */
xt_value *xt_wrk_box(xt_wrk_link *link) {
  xt_value *box = (xt_value *)malloc(sizeof(xt_value));
  if (!box) abort();
  *box = (xt_value)(uintptr_t)link;
  return box;
}

xt_wrk_link *xt_wrk_unbox(void *userdata) {
  if (!userdata) return NULL;
  return (xt_wrk_link *)(uintptr_t)(*(xt_value *)userdata);
}

/* -- EventEmitter --------------------------------------------------------- */

static xt_value xt_wrk_event_map(xt_value target, int create) {
  xt_value map = xt_wrk_get(target, "__xt_events");
  if (!XT_IS_OBJECT(map) && create) {
    map = xt_object_new();
    xt_wrk_set(target, "__xt_events", map);
  }
  return map;
}

static xt_value xt_wrk_listeners(xt_value target, const char *name, int create) {
  xt_value map = xt_wrk_event_map(target, create);
  if (!XT_IS_OBJECT(map)) return XT_UNDEFINED;
  xt_value key = xt_wrk_string(name);
  xt_value array = xt_object_get(map, key);
  if (!XT_IS_ARRAY(array) && create) {
    array = xt_array_new(0, NULL);
    xt_object_set(map, key, array);
  }
  return array;
}

static xt_value xt_wrk_pending_map(xt_value target, int create) {
  xt_value map = xt_wrk_get(target, "__xt_pending");
  if (!XT_IS_OBJECT(map) && create) {
    map = xt_object_new();
    xt_wrk_set(target, "__xt_pending", map);
  }
  return map;
}

void xt_wrk_emit(xt_value target, const char *name, int32_t argc, xt_value *argv) {
  xt_value listeners = xt_wrk_listeners(target, name, 0);
  int32_t count = XT_IS_ARRAY(listeners) ? xt_array_size(listeners) : 0;
  if (count > 0) {
    /* Copy the list: a listener may add or remove handlers, and a `once`
     * handler removes itself before it runs. */
    xt_value snapshot = xt_array_new(0, NULL);
    for (int32_t i = 0; i < count; i++) xt_array_push(snapshot, xt_array_items(listeners)[i]);
    count = xt_array_size(snapshot);
    for (int32_t i = 0; i < count; i++) {
      xt_value fn = xt_array_items(snapshot)[i];
      if (!XT_IS_FUNCTION(fn)) continue;
      if (xt_truthy(xt_wrk_get(fn, "__xt_once"))) xt_wrk_off(target, name, fn);
      xt_call_with_this(fn, target, argc, argv);
    }
    return;
  }
  /* Buffer until a listener exists, mirroring the node extension's emitter. */
  xt_value map = xt_wrk_pending_map(target, 1);
  xt_value key = xt_wrk_string(name);
  xt_value queue = xt_object_get(map, key);
  if (!XT_IS_ARRAY(queue)) {
    queue = xt_array_new(0, NULL);
    xt_object_set(map, key, queue);
  }
  xt_array_push(queue, xt_array_new(argc, argv));
}

void xt_wrk_emit0(xt_value target, const char *name) { xt_wrk_emit(target, name, 0, NULL); }

void xt_wrk_emit1(xt_value target, const char *name, xt_value arg) {
  xt_wrk_emit(target, name, 1, &arg);
}

/* `once` wraps the handler so the emitter can recognise and drop it after the
 * first call without the handler needing a self-reference. */
static xt_value xt_wrk_once_call(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value handler = xt_closure_env(env, 0);
  return xt_call_with_this(handler, thisValue, argc, argv);
}

static void xt_wrk_flush_pending(xt_value target, const char *name) {
  xt_value map = xt_wrk_pending_map(target, 0);
  if (!XT_IS_OBJECT(map)) return;
  xt_value key = xt_wrk_string(name);
  xt_value queue = xt_object_get(map, key);
  if (!XT_IS_ARRAY(queue) || xt_array_size(queue) == 0) return;
  xt_object_set(map, key, xt_array_new(0, NULL));
  int32_t count = xt_array_size(queue);
  for (int32_t i = 0; i < count; i++) {
    xt_value record = xt_array_items(queue)[i];
    int32_t argc = xt_array_size(record);
    if (argc > 8) argc = 8;
    xt_wrk_emit(target, name, argc, xt_array_items(record));
  }
}

void xt_wrk_on(xt_value target, const char *name, xt_value fn, int once) {
  if (!XT_IS_FUNCTION(fn)) return;
  xt_value listener = fn;
  if (once) {
    xt_value environment[1] = {fn};
    listener = xt_closure_new((void *)xt_wrk_once_call, 1, environment);
    xt_wrk_set(listener, "__xt_once", XT_TRUE);
  }
  xt_value array = xt_wrk_listeners(target, name, 1);
  xt_array_push(array, listener);
  xt_wrk_flush_pending(target, name);
  if (strcmp(name, "message") == 0) xt_wrk_listen_for_target(target);
}

/* A listener is stored either directly or wrapped by `once`; `off` matches
 * both the handler the program passed and the wrapper it was given. */
static int xt_wrk_listener_matches(xt_value item, xt_value fn) {
  if (item == fn) return 1;
  if (xt_wrk_get(item, "__xt_original") == fn) return 1;
  return xt_truthy(xt_wrk_get(item, "__xt_once")) && xt_closure_env(item, 0) == fn;
}

void xt_wrk_off(xt_value target, const char *name, xt_value fn) {
  xt_value array = xt_wrk_listeners(target, name, 0);
  if (!XT_IS_ARRAY(array)) return;
  xt_value kept = xt_array_new(0, NULL);
  int32_t count = xt_array_size(array);
  for (int32_t i = 0; i < count; i++) {
    xt_value item = xt_array_items(array)[i];
    if (!xt_wrk_listener_matches(item, fn)) xt_array_push(kept, item);
  }
  xt_value map = xt_wrk_event_map(target, 1);
  xt_object_set(map, xt_wrk_string(name), kept);
}

void xt_wrk_remove_all(xt_value target, const char *name) {
  xt_value map = xt_wrk_event_map(target, 1);
  xt_object_set(map, xt_wrk_string(name), xt_array_new(0, NULL));
}

int32_t xt_wrk_listener_count(xt_value target, const char *name) {
  xt_value array = xt_wrk_listeners(target, name, 0);
  return XT_IS_ARRAY(array) ? xt_array_size(array) : 0;
}

/* -- collector roots ------------------------------------------------------ */

static void xt_wrk_gc_scan(void) {
  for (xt_wrk_link *link = xt_wrk_parents; link; link = link->next) {
    xt_gc_mark_value(link->target);
    xt_gc_mark_value(link->terminate_promise);
  }
  if (xt_wrk_scope_channel) xt_gc_mark_value(xt_wrk_scope_channel->target);
  xt_gc_mark_value(xt_wrk_data_cache);
}

void xt_wrk_gc_register(void) {
  if (xt_wrk_gc_registered) return;
  xt_wrk_gc_registered = 1;
  xt_gc_register_root_provider(xt_wrk_gc_scan);
}

/* -- environment / worker data -------------------------------------------- */

int xt_wrk_is_worker(void) {
  const char *marker = xt_wrk_env_get(XT_WRK_ENV_MARKER);
  return marker != NULL && marker[0] != 0;
}

int xt_wrk_is_main_thread(void) { return !xt_wrk_is_worker(); }

const char *xt_wrk_scope_name(void) {
  const char *name = xt_wrk_env_get(XT_WRK_ENV_NAME);
  return name ? name : "";
}

xt_value xt_wrk_worker_data(void) {
  if (!xt_wrk_is_worker()) return XT_UNDEFINED;
  if (xt_wrk_data_cache != XT_UNDEFINED) return xt_wrk_data_cache;
  const char *text = xt_wrk_env_get(XT_WRK_ENV_DATA);
  if (!text || !text[0]) return XT_UNDEFINED;
  xt_wrk_data_cache = xt_wrk_decode(text, strlen(text));
  return xt_wrk_data_cache;
}

xt_value xt_wrk_scope_data(void) { return xt_wrk_worker_data(); }

char *xt_wrk_encode_data(xt_value value, size_t *length) {
  if (value == XT_UNDEFINED) {
    *length = 0;
    return NULL;
  }
  return xt_wrk_encode(value, length);
}

/* -- entry validation ----------------------------------------------------- */

const char *xt_wrk_program_path(void) {
  if (!xt_wrk_program_path_cache) {
    xt_value value = xt_import_meta(xt_wrk_string("filename"));
    const char *text = XT_IS_STRING(value) ? xt_string_data(value) : "";
    xt_wrk_program_path_cache = xt_wrk_duplicate(text);
  }
  return xt_wrk_program_path_cache;
}

static const char *xt_wrk_basename(const char *path) {
  const char *base = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  return base;
}

static int xt_wrk_name_equals(const char *a, const char *b) {
#if defined(_WIN32)
  return _stricmp(a, b) == 0;
#else
  return strcmp(a, b) == 0;
#endif
}

void xt_wrk_require_self_script(xt_value script) {
  if (script == XT_UNDEFINED || script == XT_NULL) {
    xt_throw(xt_error_from_message("TypeError: Worker requires a script URL"));
    return;
  }
  xt_value text = xt_to_string(script);
  xt_string *string = xt_as_string(text);
  const char *raw = xt_string_bytes(string);
  char path[4096];
  size_t length = string->length < sizeof(path) - 1 ? string->length : sizeof(path) - 1;
  memcpy(path, raw, length);
  path[length] = 0;
  char *cut = strpbrk(path, "?#");
  if (cut) *cut = 0;
  const char *base = xt_wrk_basename(path);
  char *program = xt_wrk_duplicate(xt_wrk_basename(xt_wrk_program_path()));
  int matches = program && xt_wrk_name_equals(base, program);
#if defined(_WIN32)
  if (!matches && program) {
    size_t n = strlen(program);
    if (n > 4 && _stricmp(program + n - 4, ".exe") == 0) {
      program[n - 4] = 0;
      matches = xt_wrk_name_equals(base, program);
    }
  }
#endif
  free(program);
  if (matches) return;
  char message[1024];
  snprintf(message, sizeof(message),
           "Error: worker script '%s' is not part of this binary. xbintsc compiles "
           "every worker into the executable, so pass import.meta.filename and "
           "branch on isMainThread.",
           path);
  xt_throw(xt_error_from_message(message));
}

/* -- worker scope (the `self` / `parentPort` side) ------------------------ */

/* Close the parent connection from the normal exit path. Letting the process
 * teardown close it instead can abort the socket (TCP reset) before the parent
 * has read the last frame, because the parent may still be starting other
 * workers when this one finishes. */
static void xt_wrk_scope_shutdown(void) {
  if (xt_wrk_scope_channel) xt_wrk_link_close(xt_wrk_scope_channel);
}

static void xt_wrk_scope_record_shutdown(void) {
  static int installed = 0;
  if (installed) return;
  installed = 1;
  atexit(xt_wrk_scope_shutdown);
}

static xt_wrk_link *xt_wrk_scope_record(void) {
  if (xt_wrk_scope_channel) return xt_wrk_scope_channel;
  xt_wrk_link *link = (xt_wrk_link *)calloc(1, sizeof(xt_wrk_link));
  if (!link) abort();
  link->target = XT_UNDEFINED;
  link->terminate_promise = XT_UNDEFINED;
  link->listen_fd = -1;
  link->fd = -1;
  const char *mode = xt_wrk_env_get(XT_WRK_ENV_MODE);
  link->web_mode = mode != NULL && strcmp(mode, "web") == 0;
  xt_wrk_scope_channel = link;
  xt_wrk_gc_register();
  xt_wrk_scope_record_shutdown();
  return link;
}

static void xt_wrk_scope_read_ready(void *userdata, int events) {
  xt_wrk_link *link = xt_wrk_unbox(userdata);
  if (!link || link->fd < 0) return;
  if (events & XT_IO_READ) {
    char chunk[4096];
    int closed = 0;
    for (;;) {
      long got = xt_wrk_recv_some(link->fd, chunk, sizeof(chunk));
      if (got > 0) {
        xt_wrk_in_append(link, chunk, (size_t)got);
        continue;
      }
      if (got != -2) closed = 1;
      break;
    }
    xt_wrk_consume_frames(link);
    if (closed) {
      xt_wrk_link_close(link);
      return;
    }
  }
  if (events & XT_IO_WRITE) xt_wrk_flush(link);
}

/* Connect to the parent on first use. The parent queues anything posted before
 * the handshake, so connecting late is safe. */
int xt_wrk_scope_connect(void) {
  xt_wrk_link *link = xt_wrk_scope_record();
  if (link->connected) return 1;
  if (!xt_wrk_is_worker()) return 0;
  const char *port_text = xt_wrk_env_get(XT_WRK_ENV_PORT);
  const char *token = xt_wrk_env_get(XT_WRK_ENV_TOKEN);
  if (!port_text || !token) return 0;
  int fd = xt_wrk_connect_loopback(atoi(port_text));
  if (fd < 0) return 0;
  size_t token_length = strlen(token);
  size_t sent = 0;
  while (sent < token_length) {
    long written = xt_wrk_send_some(fd, token + sent, token_length - sent);
    if (written <= 0) {
      xt_wrk_close_fd(fd);
      return 0;
    }
    sent += (size_t)written;
  }
  link->fd = fd;
  link->connected = 1;
  if (link->web_mode) xt_wrk_scope_listen();
  return 1;
}

int xt_wrk_scope_connected(void) {
  return xt_wrk_scope_channel != NULL && xt_wrk_scope_channel->connected;
}

void xt_wrk_scope_listen(void) {
  if (!xt_wrk_scope_connect()) return;
  xt_wrk_link *link = xt_wrk_scope_channel;
  if (link->listening || link->fd < 0) return;
  if (!link->waiter_box) link->waiter_box = xt_wrk_box(link);
  if (xt_loop_add(link->fd, XT_IO_READ | (link->out_length ? XT_IO_WRITE : 0),
                  xt_wrk_scope_read_ready, link->waiter_box) != 0) {
    return;
  }
  link->listening = 1;
}

xt_value xt_wrk_scope_object(void) {
  xt_wrk_link *link = xt_wrk_scope_record();
  if (link->target == XT_UNDEFINED) {
    xt_value object = xt_object_new();
    xt_object_set(object, xt_wrk_string("__xt_scope"), XT_TRUE);
    link->target = object;
  }
  if (xt_wrk_is_worker() && !link->connected) xt_wrk_scope_connect();
  return link->target;
}

void xt_wrk_scope_post(xt_value value) {
  if (!xt_wrk_is_worker()) {
    xt_throw(xt_error_from_message(
        "Error: postMessage is only available inside a worker"));
    return;
  }
  if (!xt_wrk_scope_connect()) {
    xt_throw(xt_error_from_message("Error: worker is not connected to its parent"));
    return;
  }
  size_t length = 0;
  char *encoded = xt_wrk_encode(value, &length);
  xt_wrk_queue_frame(xt_wrk_scope_channel, encoded, length);
  free(encoded);
}

void xt_wrk_scope_close(void) {
  if (xt_wrk_scope_channel) xt_wrk_link_close(xt_wrk_scope_channel);
}

void xt_wrk_listen_for_target(xt_value target) {
  if (xt_wrk_scope_channel && xt_wrk_scope_channel->target == target) xt_wrk_scope_listen();
}
