/*
 * Node.js `fs.readFileSync` for xbintsc.
 *
 * Compiled and linked only when the `node` extension is registered. Following
 * the runtime calling convention, the builtin is:
 *     xt_value fn(int32_t argc, xt_value *argv)
 *
 * `readFileSync(path[, options])` reads a file and returns its contents as a
 * string. `options` may be an encoding string or an object with an `encoding`
 * property; `"utf8"` (the default), `"ascii"`, `"latin1"` and `"binary"` return
 * the raw bytes, while `"hex"` and `"base64"` return encoded strings. There is
 * no `Buffer` type yet, so binary reads always come back as text.
 */

#include "rt.h"
#include "fs_common.h"

xt_value xt_node_read_text_file(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  xt_value pathValue = xt_to_string(argv[0]);
  const char *path = xt_string_data(pathValue);
  if (!path) return xt_undefined();

  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "xbintsc: cannot open '%s'\n", path);
    return xt_undefined();
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size < 0) {
    fclose(file);
    return xt_undefined();
  }
  char *buffer = (char *)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(file);
    return xt_undefined();
  }
  size_t read = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read] = '\0';

  const char *encoding = argc > 1 ? xt_fs_encoding(argv[1]) : NULL;
  xt_value result = xt_fs_encode_bytes((const unsigned char *)buffer, read, encoding);
  free(buffer);
  return result;
}
