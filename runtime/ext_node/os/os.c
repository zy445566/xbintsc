/*
 * Node.js `os` module for xbintsc.
 *
 * Exposed through namespace dispatch: `os.<name>()` lowers to
 * `xt_os_static(<name>, argc, argv)`. Implemented: platform, arch, type,
 * release, endianness, homedir, tmpdir, hostname, totalmem, freemem.
 */

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

static const char *xt_os_platform(void) {
#if defined(_WIN32)
  return "win32";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#elif defined(__FreeBSD__)
  return "freebsd";
#else
  return "unknown";
#endif
}

static const char *xt_os_type(void) {
#if defined(_WIN32)
  return "Windows_NT";
#elif defined(__APPLE__)
  return "Darwin";
#elif defined(__linux__)
  return "Linux";
#elif defined(__FreeBSD__)
  return "FreeBSD";
#else
  return "Unknown";
#endif
}

static const char *xt_os_arch(void) {
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

static xt_value xt_os_string(const char *value) {
  return value ? xt_string_from_cstr(value) : xt_undefined();
}

static xt_value xt_os_totalmem(void) {
#if defined(_WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) return xt_number((double)status.ullTotalPhys);
  return xt_number(0);
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long pageSize = sysconf(_SC_PAGESIZE);
  if (pages <= 0 || pageSize <= 0) return xt_number(0);
  return xt_number((double)pages * (double)pageSize);
#endif
}

static xt_value xt_os_freemem(void) {
#if defined(_WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) return xt_number((double)status.ullAvailPhys);
  return xt_number(0);
#elif defined(_SC_AVPHYS_PAGES)
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long pageSize = sysconf(_SC_PAGESIZE);
  if (pages <= 0 || pageSize <= 0) return xt_number(0);
  return xt_number((double)pages * (double)pageSize);
#else
  /* macOS exposes free memory through Mach APIs only; report total memory. */
  return xt_os_totalmem();
#endif
}

xt_value xt_os_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (!method) return xt_undefined();
  (void)argc;
  (void)argv;

  if (strcmp(method, "platform") == 0) return xt_string_from_cstr(xt_os_platform());
  if (strcmp(method, "type") == 0) return xt_string_from_cstr(xt_os_type());
  if (strcmp(method, "arch") == 0) return xt_string_from_cstr(xt_os_arch());
  if (strcmp(method, "endianness") == 0) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return xt_string_from_cstr("BE");
#else
    return xt_string_from_cstr("LE");
#endif
  }
  if (strcmp(method, "homedir") == 0) {
#if defined(_WIN32)
    return xt_os_string(getenv("USERPROFILE"));
#else
    return xt_os_string(getenv("HOME"));
#endif
  }
  if (strcmp(method, "tmpdir") == 0) {
#if defined(_WIN32)
    const char *temp = getenv("TEMP");
    return xt_os_string(temp ? temp : "C:\\Windows\\Temp");
#else
    const char *temp = getenv("TMPDIR");
    return xt_os_string(temp ? temp : "/tmp");
#endif
  }
  if (strcmp(method, "hostname") == 0) {
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) != 0) return xt_undefined();
    buffer[sizeof(buffer) - 1] = '\0';
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "release") == 0) {
#if defined(_WIN32)
    return xt_string_from_cstr("0.0.0");
#else
    struct utsname info;
    if (uname(&info) != 0) return xt_undefined();
    return xt_string_from_cstr(info.release);
#endif
  }
  if (strcmp(method, "totalmem") == 0) return xt_os_totalmem();
  if (strcmp(method, "freemem") == 0) return xt_os_freemem();
  if (strcmp(method, "cpus") == 0) {
    /* No per-core details yet; report the logical processor count as length. */
    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (processors <= 0) processors = 1;
    xt_value *items = (xt_value *)malloc(sizeof(xt_value) * (size_t)processors);
    if (!items) return xt_array_new(0, NULL);
    for (long i = 0; i < processors; i++) {
      xt_value info = xt_object_new();
      xt_object_set(info, xt_string_from_cstr("model"), xt_string_from_cstr("unknown"));
      xt_object_set(info, xt_string_from_cstr("speed"), xt_number(0));
      items[i] = info;
    }
    xt_value result = xt_array_new((int32_t)processors, items);
    free(items);
    return result;
  }
  return xt_undefined();
}
