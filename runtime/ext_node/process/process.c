/*
 * Node.js `process` object for xbintsc.
 *
 * The compiler lowers `process.<method>(...)` to
 * `xt_process_call(<method>, argc, argv)`, `process.<property>` to
 * `xt_process_get(<property>)` and `process.<property> = value` to
 * `xt_process_set(<property>, value)`.
 *
 * Besides the original cwd/exit/uptime/hrtime/getuid methods and the
 * platform/arch/pid/argv/env properties, the object is an EventEmitter (so the
 * TUI can subscribe to "exit") and owns the three standard streams. `xt_finish`
 * runs the "exit" listeners once, after the event loop drains, and returns the
 * status `process.exitCode`/`process.exit(n)` asked for.
 */

#include "../node_common.h"
#include "streams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
extern char **_environ;
#define XT_ENVIRON _environ
#include <direct.h>
#include <process.h>
#include <windows.h>
#define xt_getpid _getpid
#define xt_process_getcwd _getcwd
#else
extern char **environ;
#define XT_ENVIRON environ
#include <unistd.h>
#define xt_getpid getpid
#define xt_process_getcwd getcwd
#endif

static xt_value xt_process_emitter_value = 0;
static xt_value xt_process_versions_value = 0;
/* Copied into a static buffer: the string's bytes are collected with it. */
static char xt_process_title_value[256] = "xbintsc";
static int xt_process_status_set = 0;

static const char *xt_process_platform(void) {
#if defined(_WIN32)
  return "win32";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

static const char *xt_process_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
  return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
  return "ia32";
#elif defined(__arm__)
  return "arm";
#else
  return "unknown";
#endif
}

static xt_value xt_process_env(void) {
  xt_value object = xt_object_new();
  if (!XT_ENVIRON) return object;
  for (char **entry = XT_ENVIRON; *entry; entry++) {
    const char *equals = strchr(*entry, '=');
    if (!equals) continue;
    xt_value key = xt_string_new(*entry, (size_t)(equals - *entry));
    xt_object_set(object, key, xt_string_from_cstr(equals + 1));
  }
  return object;
}

static xt_value xt_process_argv(void) {
  int32_t count = xt_program_argc;
  /* Fill an `xt_array` element by element rather than a raw `xt_value` buffer:
     the collector cannot see a plain malloc'd buffer, so strings created here
     would be swept as soon as a later element allocates (demonstrated by
     `process.argv` returning recycled strings under GC stress). The array is a
     stack local, so the conservative scan keeps it and its elements alive. */
  xt_value result = xt_array_new(0, NULL);
  if (count <= 0 || !xt_program_argv) return result;
  /* Node semantics: `[execPath, scriptPath, ...args]`. A standalone binary is
     both the executable and the script, so the executable path is repeated;
     user arguments therefore start at index 2, matching `node script.js a b`. */
  xt_array_push(result, xt_string_from_cstr(xt_program_argv[0]));
  for (int32_t i = 0; i < count; i++) xt_array_push(result, xt_string_from_cstr(xt_program_argv[i]));
  return result;
}

/* -- emitter / termination ------------------------------------------------ */

static void xt_process_run_exit_listeners(void) {
  if (!xt_process_emitter_value) return;
  xt_value code = xt_number((double)xt_get_exit_status());
  xt_node_emit(xt_process_emitter_value, "exit", 1, &code);
  /* Exit handlers may queue further work (`process.nextTick`); Node drops it,
     but draining keeps a handler that prints through a promise visible. */
  xt_drain_microtasks();
}

static xt_value xt_process_emitter(void) {
  xt_gc_add_root(&xt_process_emitter_value);
  if (!xt_process_emitter_value) {
    xt_value emitter = xt_object_new();
    xt_node_install_emitter(emitter);
    xt_process_emitter_value = emitter;
    xt_at_exit(xt_process_run_exit_listeners);
  }
  return xt_process_emitter_value;
}

static xt_value xt_process_versions(void) {
  xt_gc_add_root(&xt_process_versions_value);
  if (!xt_process_versions_value) {
    xt_value versions = xt_object_new();
    /* xbintsc is not Node; expose the shape the ecosystem probes for
       (`process.versions.node`, `process.versions.bun`) without lying about
       the runtime. A missing key is `undefined`, which is what the `bun`
       checks in portable code expect outside Bun. */
    xt_node_set(versions, "node", xt_string_from_cstr("0.0.0"));
    xt_process_versions_value = versions;
  }
  return xt_process_versions_value;
}

/* -- method dispatch ------------------------------------------------------ */

xt_value xt_process_call(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return xt_undefined();

  if (strcmp(method, "on") == 0 || strcmp(method, "once") == 0 || strcmp(method, "addListener") == 0 ||
      strcmp(method, "prependListener") == 0) {
    return xt_node_on_impl(xt_process_emitter(), XT_UNDEFINED, argc, argv);
  }
  if (strcmp(method, "off") == 0 || strcmp(method, "removeListener") == 0 ||
      strcmp(method, "removeAllListeners") == 0) {
    return xt_node_remove_listener_impl(xt_process_emitter(), XT_UNDEFINED, argc, argv);
  }
  if (strcmp(method, "emit") == 0) {
    return xt_node_emit_impl(xt_process_emitter(), XT_UNDEFINED, argc, argv);
  }
  if (strcmp(method, "nextTick") == 0) {
    /* The microtask queue is the closest thing to Node's next-tick queue: it
       drains before the event loop resumes and after the current stack. */
    if (argc > 0 && XT_IS_FUNCTION(argv[0])) xt_queue_microtask(argc, argv);
    return xt_undefined();
  }
  if (strcmp(method, "cwd") == 0) {
    char buffer[4096];
    if (!xt_process_getcwd(buffer, sizeof(buffer))) return xt_undefined();
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "exit") == 0) {
    if (argc > 0 && !xt_truthy(xt_is_nullish(argv[0]))) {
      xt_set_exit_status((int)xt_to_number(argv[0]));
      xt_process_status_set = 1;
    }
    /* Run the "exit" listeners (once) before leaving, then exit with the
       status the program asked for — including one set by a listener. */
    xt_finish();
    exit(xt_get_exit_status());
    return xt_undefined();
  }
  if (strcmp(method, "uptime") == 0) {
    return xt_number((double)clock() / (double)CLOCKS_PER_SEC);
  }
  if (strcmp(method, "hrtime") == 0) {
    xt_value items[2];
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    items[0] = xt_number((double)(counter.QuadPart / frequency.QuadPart));
    items[1] = xt_number((double)((counter.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart));
#else
    struct timespec now;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &now);
#else
    now.tv_sec = (long)time(NULL);
    now.tv_nsec = 0;
#endif
    items[0] = xt_number((double)now.tv_sec);
    items[1] = xt_number((double)now.tv_nsec);
#endif
    return xt_array_new(2, items);
  }
  if (strcmp(method, "getuid") == 0) {
#if defined(_WIN32)
    return xt_number(0);
#else
    return xt_number((double)getuid());
#endif
  }
  return xt_undefined();
}

/* -- property writes ------------------------------------------------------ */

xt_value xt_process_set(xt_value name, xt_value value) {
  const char *key = xt_string_data(name);
  if (!key) return value;
  if (strcmp(key, "exitCode") == 0) {
    if (xt_truthy(xt_is_nullish(value))) {
      xt_set_exit_status(0);
      xt_process_status_set = 0;
      return value;
    }
    xt_set_exit_status((int)xt_to_number(value));
    xt_process_status_set = 1;
    return value;
  }
  if (strcmp(key, "title") == 0) {
    const char *title = xt_node_cstr(value);
    xt_process_title_value[0] = '\0';
    if (title) {
      strncpy(xt_process_title_value, title, sizeof(xt_process_title_value) - 1);
      xt_process_title_value[sizeof(xt_process_title_value) - 1] = '\0';
    }
    return value;
  }
  return value;
}

/* -- property reads ------------------------------------------------------- */

xt_value xt_process_get(xt_value name) {
  const char *key = xt_string_data(name);
  if (!key) return xt_undefined();

  if (strcmp(key, "stdout") == 0) return xt_process_stdout_object();
  if (strcmp(key, "stderr") == 0) return xt_process_stderr_object();
  if (strcmp(key, "stdin") == 0) return xt_process_stdin_object();
  if (strcmp(key, "versions") == 0) return xt_process_versions();
  if (strcmp(key, "exitCode") == 0) {
    if (!xt_process_status_set) return xt_undefined();
    return xt_number((double)xt_get_exit_status());
  }
  if (strcmp(key, "execPath") == 0) {
    if (xt_program_argc <= 0 || !xt_program_argv) return xt_undefined();
    return xt_string_from_cstr(xt_program_argv[0]);
  }
  if (strcmp(key, "platform") == 0) return xt_string_from_cstr(xt_process_platform());
  if (strcmp(key, "arch") == 0) return xt_string_from_cstr(xt_process_arch());
  if (strcmp(key, "pid") == 0) return xt_number((double)xt_getpid());
  if (strcmp(key, "ppid") == 0) {
#if defined(_WIN32)
    return xt_number(0);
#else
    return xt_number((double)getppid());
#endif
  }
  if (strcmp(key, "version") == 0) return xt_string_from_cstr("v0.0.0");
  if (strcmp(key, "argv") == 0) return xt_process_argv();
  if (strcmp(key, "env") == 0) return xt_process_env();
  if (strcmp(key, "title") == 0) return xt_string_from_cstr(xt_process_title_value);
  return xt_undefined();
}
