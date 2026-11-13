/*
 * xbintsc runtime — worker transport internals.
 *
 * Shared by the two public worker APIs:
 *
 *   - the Web Worker globals (`Worker`, `MessageChannel`, `self`) contributed
 *     by the `webworker` extension, and
 *   - Node's `worker_threads` module contributed by the `node` extension.
 *
 * xbintsc has no threads (the value model, the collector and the event loop are
 * single-threaded), so a worker is a **second process** running the same
 * executable. The two processes talk over a loopback TCP socket created by the
 * parent; the port and a one-shot token travel through the environment, which
 * is the only portable bootstrap channel (`fork`/`execvp` on POSIX,
 * `_spawnvp`/`CreateProcess` on Windows).
 *
 * A socket (rather than stdin/stdout pipes) is what makes the design portable:
 * the reactor selects on sockets on both platforms, Windows `select(2)` cannot
 * wait on pipe handles, and the worker's own `console.log` keeps using the
 * inherited stdout instead of corrupting a private protocol stream.
 */
#ifndef XT_WORKER_INTERNAL_H
#define XT_WORKER_INTERNAL_H

#include "rt_internal.h"

#include <stdint.h>

/* Environment variables the parent sets on the child. */
#define XT_WRK_ENV_MARKER "XBINTSC_WORKER"
#define XT_WRK_ENV_PORT "XBINTSC_WORKER_PORT"
#define XT_WRK_ENV_TOKEN "XBINTSC_WORKER_TOKEN"
#define XT_WRK_ENV_NAME "XBINTSC_WORKER_NAME"
#define XT_WRK_ENV_DATA "XBINTSC_WORKER_DATA"
#define XT_WRK_ENV_MODE "XBINTSC_WORKER_MODE" /* "web" or "node" */

/** Largest frame either side accepts; guards against a corrupt length word. */
#define XT_WRK_MAX_FRAME (64u * 1024u * 1024u)

/* A live worker in the parent process. Opaque to the API layers. */
typedef struct xt_wrk_link xt_wrk_link;

/* A child process identifier: `pid_t` on POSIX, a process HANDLE on Windows.
 * `intptr_t` holds either. */
typedef intptr_t xt_wrk_pid;

/* One control channel. Parent side: one per live worker. Worker side: the
 * single connection to the parent. */
struct xt_wrk_link {
  struct xt_wrk_link *next;
  xt_value target;
  xt_value terminate_promise;
  xt_wrk_pid pid;
  int listen_fd;
  int fd;
  int connected;
  int handshaking; /* parent: the child's token has not been seen yet */
  int listening;   /* a read watcher is registered */
  int exited;
  int exit_code;
  int terminating;
  int web_mode;
  int thread_id;
  char token[24];
  size_t token_length;
  char *in;
  size_t in_length;
  size_t in_capacity;
  char *out;
  size_t out_length;
  size_t out_capacity;
  xt_value *waiter_box; /* `xt_loop_add` userdata (pointer kept in a value) */
};

/* -- operating-system half (worker_platform.c) ---------------------------- */
int xt_wrk_env_set(const char *key, const char *value);
void xt_wrk_env_unset(const char *key);
const char *xt_wrk_env_get(const char *key);
char *xt_wrk_duplicate(const char *text);
void xt_wrk_make_token(char *out, size_t capacity);
int xt_wrk_would_block(void);
void xt_wrk_close_fd(int fd);
int xt_wrk_set_nonblocking(int fd);
int xt_wrk_listen_loopback(int *out_port);
int xt_wrk_accept(int listen_fd);
int xt_wrk_connect_loopback(int port);
long xt_wrk_send_some(int fd, const char *data, size_t length);
long xt_wrk_recv_some(int fd, char *data, size_t capacity);
/** Wait until `fd` is writable (or the timeout expires). 1 on writable. */
int xt_wrk_wait_writable(int fd, int timeout_ms);
xt_wrk_pid xt_wrk_spawn_process(const char *path, char **error);
/** 1 when ended (code in `*exit_code`), 0 while running. Reaps the child. */
int xt_wrk_process_exited(xt_wrk_pid pid, int *exit_code);
void xt_wrk_kill_process(xt_wrk_pid pid);

/* -- event delivery ------------------------------------------------------- */
/*
 * A small EventEmitter shared by every worker object. Listeners live in an
 * `__xt_events` property (event name -> array of functions); an event emitted
 * before a listener exists is buffered in `__xt_pending` and replayed by the
 * first `on`/`addEventListener`. That mirrors Node's emitter and keeps the
 * browser surface working when a worker delivers a message before the program
 * has attached its handler.
 */
void xt_wrk_emit(xt_value target, const char *name, int32_t argc, xt_value *argv);
void xt_wrk_emit0(xt_value target, const char *name);
void xt_wrk_emit1(xt_value target, const char *name, xt_value arg);
void xt_wrk_on(xt_value target, const char *name, xt_value fn, int once);
void xt_wrk_off(xt_value target, const char *name, xt_value fn);
void xt_wrk_remove_all(xt_value target, const char *name);
int32_t xt_wrk_listener_count(xt_value target, const char *name);

/* -- parent side ---------------------------------------------------------- */

/** True when this process is the main program (no marker in the environment). */
int xt_wrk_is_main_thread(void);
/** `worker.name` / `self.name`; "" on the main thread. */
const char *xt_wrk_scope_name(void);
/** `workerData`, decoded once; `XT_UNDEFINED` on the main thread. */
xt_value xt_wrk_worker_data(void);
/** Decoded `workerData` for a spawn (never throws). */
char *xt_wrk_encode_data(xt_value value, size_t *length);

/*
 * Spawn `path` as a worker. `data` is the workerData value (XT_UNDEFINED for
 * none), `name` the worker name, `web_mode` selects browser lifetime rules
 * (a Web Worker stays alive waiting for messages; a Node worker only does when
 * a `message` listener keeps the port referenced). On failure returns NULL and
 * stores a malloc'd message in `*error` (caller frees).
 */
xt_wrk_link *xt_wrk_spawn(const char *path, xt_value data, const char *name, int web_mode,
                          xt_value target, char **error);
void xt_wrk_post(xt_wrk_link *link, xt_value value, int32_t argc, xt_value *argv);
void xt_wrk_terminate(xt_wrk_link *link);
double xt_wrk_thread_id(xt_wrk_link *link);
int xt_wrk_link_alive(xt_wrk_link *link);
/* Node `ref`/`unref`: whether the link keeps this process's event loop alive. */
void xt_wrk_ref(xt_wrk_link *link);
void xt_wrk_unref(xt_wrk_link *link);

/* -- worker side ---------------------------------------------------------- */

/** True when this process was started as a worker (marker present). */
int xt_wrk_is_worker(void);
/** Connect to the parent on first use. Returns 1 when connected. */
int xt_wrk_scope_connect(void);
/** Whether the scope socket is connected. */
int xt_wrk_scope_connected(void);
/** The `self` / `parentPort` object receiving messages. */
xt_value xt_wrk_scope_object(void);
xt_value xt_wrk_scope_data(void);
void xt_wrk_scope_post(xt_value value);
void xt_wrk_scope_close(void);
/** Start reading parent messages; called when a message listener is attached. */
void xt_wrk_scope_listen(void);

/* -- message codec -------------------------------------------------------- */

/** Growable text buffer used by the wire encoder. */
typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} xt_wrk_buffer;

void xt_wrk_buffer_reserve(xt_wrk_buffer *buffer, size_t extra);
void xt_wrk_buffer_put(xt_wrk_buffer *buffer, const char *text, size_t length);
void xt_wrk_buffer_puts(xt_wrk_buffer *buffer, const char *text);
void xt_wrk_buffer_putc(xt_wrk_buffer *buffer, char value);
void xt_wrk_buffer_put_uint(xt_wrk_buffer *buffer, unsigned long value);
void xt_wrk_buffer_put_string(xt_wrk_buffer *buffer, const char *text, size_t length);
void xt_wrk_buffer_put_base64(xt_wrk_buffer *buffer, const uint8_t *data, size_t length);
uint8_t *xt_wrk_decode_base64(const char *text, size_t length, size_t *out_length);
int xt_wrk_width_of_typed_array(const char *name);

/** Encode a value for the wire; returns a malloc'd, NUL-terminated buffer. */
char *xt_wrk_encode(xt_value value, size_t *length);
/** Decode one frame; throws a TypeError on malformed input. */
xt_value xt_wrk_decode(const char *bytes, size_t length);

/* -- frame plumbing (worker_core.c) --------------------------------------- */

/** Queue one length-prefixed frame and try to flush it. */
void xt_wrk_queue_frame(xt_wrk_link *link, const char *payload, size_t length);
void xt_wrk_flush(xt_wrk_link *link);
/** Parse every complete frame in `link->in`, delivering `message` events. */
void xt_wrk_consume_frames(xt_wrk_link *link);
/** Close the socket and stop watching it (the link object stays alive). */
void xt_wrk_link_close(xt_wrk_link *link);
/** Called by `xt_wrk_on` so a message listener starts reading. */
void xt_wrk_listen_for_target(xt_value target);

/* -- state shared by core and transport ----------------------------------- */
extern xt_wrk_link *xt_wrk_parents;
extern xt_wrk_link *xt_wrk_scope_channel;
extern uint64_t xt_wrk_poll_timer;
extern int xt_wrk_thread_counter;
void xt_wrk_gc_register(void);
xt_value *xt_wrk_box(xt_wrk_link *link);
xt_wrk_link *xt_wrk_unbox(void *userdata);
void xt_wrk_in_append(xt_wrk_link *link, const char *data, size_t length);
xt_value xt_wrk_terminate_promise(xt_wrk_link *link);

/* -- JavaScript surface shared by the two APIs (worker_api.c) ------------- */
void xt_wrk_install_emitter(xt_value object);
void xt_wrk_install_dom_events(xt_value object);
xt_value xt_wrk_new_worker(int32_t argc, xt_value *argv, int web_mode);
xt_value xt_wrk_scope_api_object(void);

/* -- entry-point validation ---------------------------------------------- */

/** The executable's own path (`import.meta.filename`). */
const char *xt_wrk_program_path(void);
/**
 * Validate the `filename`/URL a Worker constructor was given. xbintsc compiles
 * every worker into the same binary, so the only script that can run is this
 * program; anything else throws with an actionable message.
 */
void xt_wrk_require_self_script(xt_value script);

#endif /* XT_WORKER_INTERNAL_H */
