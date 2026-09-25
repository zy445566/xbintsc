/*
 * Node.js `fs` symbolic/hard link operations for xbintsc.
 *
 *     linkSync, symlinkSync, readlinkSync
 *
 * Windows uses `CreateHardLink`/`CreateSymbolicLink`; `readlinkSync` is not
 * available there (documented deviation).
 */

#include "../node_common.h"
#include "fs_common.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

xt_value xt_node_link(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *existing = xt_node_cstr(argv[0]);
  const char *newPath = xt_node_cstr(argv[1]);
#if defined(_WIN32)
  if (!existing || !newPath || !CreateHardLinkA(newPath, existing, NULL)) {
    xt_fs_raise_errno(errno ? errno : EPERM, "link", newPath);
    return xt_undefined();
  }
#else
  if (!existing || !newPath || link(existing, newPath) != 0) {
    xt_fs_raise_path("link", newPath);
    return xt_undefined();
  }
#endif
  return xt_undefined();
}

xt_value xt_node_symlink(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_undefined();
  const char *target = xt_node_cstr(argv[0]);
  const char *path = xt_node_cstr(argv[1]);
  const char *type = argc > 2 ? xt_node_cstr(argv[2]) : NULL;
#if defined(_WIN32)
  DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (type && (strcmp(type, "dir") == 0 || strcmp(type, "junction") == 0)) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  if (!target || !path || !CreateSymbolicLinkA(path, target, flags)) {
    xt_fs_raise_errno(errno ? errno : EPERM, "symlink", path);
    return xt_undefined();
  }
#else
  (void)type;
  if (!target || !path || symlink(target, path) != 0) {
    xt_fs_raise_path("symlink", path);
    return xt_undefined();
  }
#endif
  return xt_undefined();
}

xt_value xt_node_readlink(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  const char *path = xt_node_cstr(argv[0]);
#if defined(_WIN32)
  (void)path;
  xt_fs_raise_errno(ENOSYS, "readlink", path);
  return xt_undefined();
#else
  if (!path) {
    xt_fs_raise_errno(ENOENT, "readlink", NULL);
    return xt_undefined();
  }
  size_t capacity = 256;
  for (;;) {
    char *buffer = (char *)malloc(capacity);
    if (!buffer) return xt_undefined();
    ssize_t length = readlink(path, buffer, capacity);
    if (length < 0) {
      free(buffer);
      xt_fs_raise_path("readlink", path);
      return xt_undefined();
    }
    if ((size_t)length < capacity) {
      buffer[length] = '\0';
      xt_value result = xt_string_from_cstr(buffer);
      free(buffer);
      return result;
    }
    free(buffer);
    capacity *= 2;
  }
#endif
}
