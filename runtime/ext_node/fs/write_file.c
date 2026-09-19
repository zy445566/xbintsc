/*
 * Node.js `fs.writeFileSync` / `fs.appendFileSync` for xbintsc.
 *
 * `writeFileSync(path, data[, options])` truncates the file before writing;
 * `appendFileSync(path, data[, options])` appends. `data` is converted to a
 * string with the usual JS rules and `options` selects an encoding:
 * `"hex"`/`"base64"` decode the string, everything else writes its UTF-8 bytes.
 */

#include "rt.h"
#include "fs_common.h"

static xt_value xt_node_write_common(int32_t argc, xt_value *argv, const char *mode) {
  if (argc < 2) return xt_undefined();
  xt_value pathValue = xt_to_string(argv[0]);
  const char *path = xt_string_data(pathValue);
  if (!path) return xt_undefined();

  xt_value dataValue = xt_to_string(argv[1]);
  const char *text = xt_string_data(dataValue);
  int32_t length = xt_string_length_value(dataValue);
  if (!text || length < 0) return xt_undefined();

  const char *encoding = argc > 2 ? xt_fs_encoding(argv[2]) : NULL;
  size_t byteLength = 0;
  unsigned char *bytes = xt_fs_decode_text(text, (size_t)length, encoding, &byteLength);
  if (!bytes) return xt_undefined();

  FILE *file = fopen(path, mode);
  if (!file) {
    fprintf(stderr, "xbintsc: cannot open '%s'\n", path);
    free(bytes);
    return xt_undefined();
  }
  if (byteLength > 0) fwrite(bytes, 1, byteLength, file);
  fclose(file);
  free(bytes);
  return xt_undefined();
}

xt_value xt_node_write_file(int32_t argc, xt_value *argv) {
  return xt_node_write_common(argc, argv, "wb");
}

xt_value xt_node_append_file(int32_t argc, xt_value *argv) {
  return xt_node_write_common(argc, argv, "ab");
}
