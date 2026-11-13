/*
 * xbintsc runtime — worker transport, parent half.
 *
 * `new Worker()` listens on 127.0.0.1:0, writes the port and a one-shot token
 * into the environment, and spawns the executable again (see
 * `worker_platform.c` for the POSIX/Windows split). The child connects back and
 * sends the token as its first bytes; from then on both directions carry
 * length-prefixed frames.
 *
 * Lifetime follows Node: the parent stays alive while a worker runs, and the
 * worker's `exit` is detected by polling the child's status — a socket EOF only
 * says the port closed, not that the process ended.
 */

#include "worker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- frame plumbing ------------------------------------------------------- */

void xt_wrk_in_append(xt_wrk_link *link, const char *data, size_t length) {
  if (link->in_length + length + 1 > link->in_capacity) {
    size_t next = link->in_capacity ? link->in_capacity : 256;
    while (next < link->in_length + length + 1) next *= 2;
    char *grown = (char *)realloc(link->in, next);
    if (!grown) abort();
    link->in = grown;
    link->in_capacity = next;
  }
  memcpy(link->in + link->in_length, data, length);
  link->in_length += length;
}

static void xt_wrk_in_consume(xt_wrk_link *link, size_t count) {
  if (count >= link->in_length) {
    link->in_length = 0;
    return;
  }
  memmove(link->in, link->in + count, link->in_length - count);
  link->in_length -= count;
}

static void xt_wrk_out_append(xt_wrk_link *link, const char *data, size_t length) {
  if (link->out_length + length + 1 > link->out_capacity) {
    size_t next = link->out_capacity ? link->out_capacity : 256;
    while (next < link->out_length + length + 1) next *= 2;
    char *grown = (char *)realloc(link->out, next);
    if (!grown) abort();
    link->out = grown;
    link->out_capacity = next;
  }
  memcpy(link->out + link->out_length, data, length);
  link->out_length += length;
}

static void xt_wrk_watch_update(xt_wrk_link *link) {
  if (!link->listening || link->fd < 0) return;
  int events = XT_IO_READ;
  if (link->out_length > 0) events |= XT_IO_WRITE;
  xt_loop_update(link->fd, events);
}

void xt_wrk_flush(xt_wrk_link *link) {
  if (!link->connected || link->fd < 0 || link->handshaking) return;
  while (link->out_length > 0) {
    long sent = xt_wrk_send_some(link->fd, link->out, link->out_length);
    if (sent < 0) {
      if (xt_wrk_would_block()) break;
      xt_wrk_link_close(link);
      return;
    }
    if (sent == 0) break;
    memmove(link->out, link->out + sent, link->out_length - (size_t)sent);
    link->out_length -= (size_t)sent;
  }
  xt_wrk_watch_update(link);
}

void xt_wrk_queue_frame(xt_wrk_link *link, const char *payload, size_t length) {
  char header[4];
  header[0] = (char)(length & 0xff);
  header[1] = (char)((length >> 8) & 0xff);
  header[2] = (char)((length >> 16) & 0xff);
  header[3] = (char)((length >> 24) & 0xff);
  xt_wrk_out_append(link, header, 4);
  xt_wrk_out_append(link, payload, length);
  xt_wrk_flush(link);
}

void xt_wrk_consume_frames(xt_wrk_link *link) {
  while (link->in_length >= 4) {
    uint32_t length = (uint32_t)(unsigned char)link->in[0] |
                      ((uint32_t)(unsigned char)link->in[1] << 8) |
                      ((uint32_t)(unsigned char)link->in[2] << 16) |
                      ((uint32_t)(unsigned char)link->in[3] << 24);
    if (length > XT_WRK_MAX_FRAME) {
      xt_wrk_link_close(link);
      return;
    }
    if (link->in_length < 4 + (size_t)length) return;
    void *frame = xt_try_enter();
    if (xt_try_setjmp(frame) == 0) {
      xt_value message = xt_wrk_decode(link->in + 4, length);
      xt_try_leave(frame);
      xt_wrk_emit1(link->target, "message", message);
    } else {
      xt_value error = xt_try_exception(frame);
      xt_try_leave(frame);
      xt_wrk_emit1(link->target, "messageerror", error);
    }
    xt_wrk_in_consume(link, 4 + (size_t)length);
  }
}

void xt_wrk_link_close(xt_wrk_link *link) {
  /* A worker that replies and then closes (or `parentPort.close()`) may still
   * have a small frame queued; give it a moment to leave the socket before the
   * descriptor is closed, or the parent would lose the last message. */
  int rounds = 0;
  while (link->connected && link->fd >= 0 && link->out_length > 0 && rounds < 20) {
    long sent = xt_wrk_send_some(link->fd, link->out, link->out_length);
    if (sent > 0) {
      memmove(link->out, link->out + sent, link->out_length - (size_t)sent);
      link->out_length -= (size_t)sent;
      continue;
    }
    if (sent < 0 && xt_wrk_would_block() && xt_wrk_wait_writable(link->fd, 50)) {
      rounds++;
      continue;
    }
    break;
  }
  if (link->listening && link->fd >= 0) {
    xt_loop_remove(link->fd);
    link->listening = 0;
  }
  if (link->fd >= 0) {
    xt_wrk_close_fd(link->fd);
    link->fd = -1;
  }
  if (link->listen_fd >= 0) {
    xt_loop_remove(link->listen_fd);
    xt_wrk_close_fd(link->listen_fd);
    link->listen_fd = -1;
  }
  if (link->waiter_box) {
    free(link->waiter_box);
    link->waiter_box = NULL;
  }
  link->connected = 0;
}

/* -- parent transport ----------------------------------------------------- */

static void xt_wrk_arm_poll(void);

static void xt_wrk_finish(xt_wrk_link *link) {
  if (link->exited) return;
  link->exited = 1;
  /* Node reports 1 for a terminated worker; SIGKILL (-9) is an implementation
   * detail of the emulation, not something a program should branch on. */
  if (link->terminating) link->exit_code = 1;
  xt_wrk_link_close(link);
  if (link->terminate_promise != XT_UNDEFINED) {
    xt_promise_fulfill(link->terminate_promise, xt_number((double)link->exit_code));
  }
  xt_wrk_emit1(link->target, "exit", xt_number((double)link->exit_code));
}

static void xt_wrk_poll_tick(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_poll_timer = 0;
  int live = 0;
  for (xt_wrk_link *link = xt_wrk_parents; link; link = link->next) {
    if (link->exited) continue;
    int code = -1;
    if (xt_wrk_process_exited(link->pid, &code)) {
      link->pid = 0;
      link->exit_code = code;
      xt_wrk_finish(link);
    } else {
      live = 1;
    }
  }
  if (live) xt_wrk_arm_poll();
}

static void xt_wrk_arm_poll(void) {
  if (xt_wrk_poll_timer != 0) return;
  xt_value arguments[2] = {xt_closure_new((void *)xt_wrk_poll_tick, 0, NULL), xt_number(20)};
  xt_value id = xt_set_timeout(2, arguments);
  xt_wrk_poll_timer = (uint64_t)xt_to_number(id);
}

static void xt_wrk_read_ready(void *userdata, int events);

static void xt_wrk_start_reading(xt_wrk_link *link) {
  if (link->listening || link->fd < 0) return;
  if (!link->waiter_box) link->waiter_box = xt_wrk_box(link);
  /* While the token is outstanding only read: queued messages must not reach a
   * connection the child has not authenticated yet. */
  int events = XT_IO_READ;
  if (!link->handshaking && link->out_length > 0) events |= XT_IO_WRITE;
  if (xt_loop_add(link->fd, events, xt_wrk_read_ready, link->waiter_box) != 0) {
    return;
  }
  link->listening = 1;
}

static void xt_wrk_accept_ready(void *userdata, int events) {
  (void)events;
  xt_wrk_link *link = xt_wrk_unbox(userdata);
  if (!link || link->listen_fd < 0) return;
  int fd = xt_wrk_accept(link->listen_fd);
  if (fd < 0) return;
  xt_loop_remove(link->listen_fd);
  xt_wrk_close_fd(link->listen_fd);
  link->listen_fd = -1;
  free(link->waiter_box);
  link->waiter_box = NULL;
  link->fd = fd;
  link->connected = 1;
  link->handshaking = 1;
  xt_wrk_start_reading(link);
  xt_wrk_emit0(link->target, "online");
}

static void xt_wrk_read_ready(void *userdata, int events) {
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
    if (link->handshaking) {
      if (link->in_length < link->token_length) {
        if (closed) xt_wrk_link_close(link);
        return;
      }
      if (memcmp(link->in, link->token, link->token_length) != 0) {
        xt_wrk_emit1(link->target, "error",
                     xt_error_from_message("Error: worker handshake failed"));
        xt_wrk_link_close(link);
        return;
      }
      xt_wrk_in_consume(link, link->token_length);
      link->handshaking = 0;
      /* The child is authenticated: release whatever was posted before it
       * connected. */
      xt_wrk_flush(link);
    }
    /* Frames that arrived in the same read as EOF are still messages: parse
     * them before the socket is closed. */
    xt_wrk_consume_frames(link);
    if (closed) {
      xt_wrk_link_close(link);
      return;
    }
  }
  if (events & XT_IO_WRITE) xt_wrk_flush(link);
}

/* -- environment guard ---------------------------------------------------- */

typedef struct {
  const char *key;
  char *value;
  int present;
} xt_wrk_env_entry;

static void xt_wrk_env_save(xt_wrk_env_entry *entry, const char *key) {
  entry->key = key;
  const char *value = xt_wrk_env_get(key);
  entry->present = value != NULL;
  entry->value = value ? xt_wrk_duplicate(value) : NULL;
}

static void xt_wrk_env_restore(xt_wrk_env_entry *entry) {
  if (entry->present && entry->value) xt_wrk_env_set(entry->key, entry->value);
  else xt_wrk_env_unset(entry->key);
  free(entry->value);
  entry->value = NULL;
}

xt_wrk_link *xt_wrk_spawn(const char *path, xt_value data, const char *name, int web_mode,
                          xt_value target, char **error) {
  *error = NULL;
  xt_wrk_gc_register();
  /* Encode before creating anything that a throw would leak. `workerData` goes
   * through the environment, so an uncloneable value must fail here. */
  size_t data_length = 0;
  char *encoded = xt_wrk_encode_data(data, &data_length);
  int port = 0;
  int listen_fd = xt_wrk_listen_loopback(&port);
  if (listen_fd < 0) {
    free(encoded);
    *error = xt_wrk_duplicate("failed to create the worker control socket");
    return NULL;
  }

  xt_wrk_link *link = (xt_wrk_link *)calloc(1, sizeof(xt_wrk_link));
  if (!link) abort();
  link->target = target;
  link->terminate_promise = XT_UNDEFINED;
  link->listen_fd = listen_fd;
  link->fd = -1;
  link->web_mode = web_mode;
  link->thread_id = xt_wrk_thread_counter++;
  xt_wrk_make_token(link->token, sizeof(link->token));
  link->token_length = strlen(link->token);

  xt_wrk_env_entry guard[6];
  xt_wrk_env_save(&guard[0], XT_WRK_ENV_MARKER);
  xt_wrk_env_save(&guard[1], XT_WRK_ENV_PORT);
  xt_wrk_env_save(&guard[2], XT_WRK_ENV_TOKEN);
  xt_wrk_env_save(&guard[3], XT_WRK_ENV_NAME);
  xt_wrk_env_save(&guard[4], XT_WRK_ENV_DATA);
  xt_wrk_env_save(&guard[5], XT_WRK_ENV_MODE);

  char port_text[16];
  snprintf(port_text, sizeof(port_text), "%d", port);
  xt_wrk_env_set(XT_WRK_ENV_MARKER, "1");
  xt_wrk_env_set(XT_WRK_ENV_PORT, port_text);
  xt_wrk_env_set(XT_WRK_ENV_TOKEN, link->token);
  xt_wrk_env_set(XT_WRK_ENV_NAME, name ? name : "");
  xt_wrk_env_set(XT_WRK_ENV_MODE, web_mode ? "web" : "node");

  /* Windows caps an environment variable at 32 KiB; report that instead of
   * silently starting a worker with no `workerData`. */
  int data_ok = encoded ? xt_wrk_env_set(XT_WRK_ENV_DATA, encoded) : 1;
  if (!data_ok) {
    for (int i = 5; i >= 0; i--) xt_wrk_env_restore(&guard[i]);
    free(encoded);
    xt_wrk_close_fd(listen_fd);
    free(link);
    *error = xt_wrk_duplicate(
        "workerData is too large for the worker environment (32 KiB limit)");
    return NULL;
  }

  xt_wrk_pid pid = xt_wrk_spawn_process(path, error);

  for (int i = 5; i >= 0; i--) xt_wrk_env_restore(&guard[i]);
  free(encoded);
  if (pid <= 0) {
    xt_wrk_close_fd(listen_fd);
    free(link);
    return NULL;
  }
  link->pid = pid;
  link->next = xt_wrk_parents;
  xt_wrk_parents = link;

  link->waiter_box = xt_wrk_box(link);
  if (xt_loop_add(listen_fd, XT_IO_READ, xt_wrk_accept_ready, link->waiter_box) != 0) {
    xt_wrk_kill_process(pid);
  }
  xt_wrk_arm_poll();
  return link;
}

void xt_wrk_post(xt_wrk_link *link, xt_value value, int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv; /* transfer lists are accepted but copy, like structured clone */
  if (!link || link->exited) {
    xt_throw(xt_error_from_message("Error: worker is not running"));
    return;
  }
  size_t length = 0;
  char *encoded = xt_wrk_encode(value, &length);
  xt_wrk_queue_frame(link, encoded, length);
  free(encoded);
}

xt_value xt_wrk_terminate_promise(xt_wrk_link *link) {
  if (link->terminate_promise == XT_UNDEFINED) link->terminate_promise = xt_promise_create();
  xt_value promise = link->terminate_promise;
  if (link->exited) {
    xt_promise_fulfill(promise, xt_number((double)link->exit_code));
  } else {
    link->terminating = 1;
    xt_wrk_kill_process(link->pid);
    xt_wrk_arm_poll();
  }
  return promise;
}

void xt_wrk_terminate(xt_wrk_link *link) {
  if (!link || link->exited) return;
  link->terminating = 1;
  xt_wrk_kill_process(link->pid);
  xt_wrk_arm_poll();
}

double xt_wrk_thread_id(xt_wrk_link *link) { return link ? (double)link->thread_id : 0; }

int xt_wrk_link_alive(xt_wrk_link *link) { return link && !link->exited; }

void xt_wrk_ref(xt_wrk_link *link) { (void)link; }
void xt_wrk_unref(xt_wrk_link *link) { (void)link; }
