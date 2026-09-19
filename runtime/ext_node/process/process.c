/*
 * Node.js `process` object for xbintsc.
 *
 * The compiler lowers `process.<method>(...)` to
 * `xt_process_call(<method>, argc, argv)` and `process.<property>` to
 * `xt_process_get(<property>)`. Provided: cwd, exit, uptime, hrtime and the
 * platform/arch/pid/argv/env properties.
 */

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
extern char **_environ;
#define XT_ENVIRON _environ
#include <process.h>
#define xt_getpid _getpid
#else
extern char **environ;
#define XT_ENVIRON environ
#include <unistd.h>
#define xt_getpid getpid
#endif

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
  if (count <= 0 || !xt_program_argv) return xt_array_new(0, NULL);
  xt_value *items = (xt_value *)malloc(sizeof(xt_value) * (size_t)count);
  if (!items) return xt_array_new(0, NULL);
  for (int32_t i = 0; i < count; i++) items[i] = xt_string_from_cstr(xt_program_argv[i]);
  xt_value result = xt_array_new(count, items);
  free(items);
  return result;
}

xt_value xt_process_call(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return xt_undefined();

  if (strcmp(method, "cwd") == 0) {
    char buffer[4096];
    if (!getcwd(buffer, sizeof(buffer))) return xt_undefined();
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "exit") == 0) {
    int code = argc > 0 ? (int)xt_to_number(argv[0]) : 0;
    exit(code);
    return xt_undefined();
  }
  if (strcmp(method, "uptime") == 0) {
    return xt_number((double)clock() / (double)CLOCKS_PER_SEC);
  }
  if (strcmp(method, "hrtime") == 0) {
    struct timespec now;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &now);
#else
    now.tv_sec = (long)time(NULL);
    now.tv_nsec = 0;
#endif
    xt_value items[2];
    items[0] = xt_number((double)now.tv_sec);
    items[1] = xt_number((double)now.tv_nsec);
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

xt_value xt_process_get(xt_value name) {
  const char *key = xt_string_data(name);
  if (!key) return xt_undefined();

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
  if (strcmp(key, "title") == 0) return xt_string_from_cstr("xbintsc");
  return xt_undefined();
}
