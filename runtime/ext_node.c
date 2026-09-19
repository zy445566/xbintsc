/*
 * Node.js compatibility extension for xbtsc.
 *
 * Compiled and linked only when the `node` extension is registered. Each
 * builtin follows the runtime calling convention: (int32_t argc, xt_value *argv).
 */

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>

xt_value xt_node_read_text_file(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_undefined();
  xt_value pathValue = xt_to_string(argv[0]);
  const char *path = xt_string_data(pathValue);
  if (!path) return xt_undefined();

  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "xbtsc: cannot open '%s'\n", path);
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
  xt_value result = xt_string_new(buffer, read);
  free(buffer);
  return result;
}
