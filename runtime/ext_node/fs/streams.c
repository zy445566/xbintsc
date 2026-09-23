/*
 * `fs.createReadStream` / `fs.createWriteStream` for xbintsc.
 *
 * xbintsc has no asynchronous file streaming, so these factories return small
 * descriptor objects understood by `stream/promises`'s `pipeline`: they carry
 * the target `_path` (and a `_kind` tag) and the pipeline reads/writes the file
 * in one shot. They also behave well enough as EventEmitters for simple
 * synchronous `on("data")` usage (see `stream.c`).
 */

#include "rt.h"
#include "../node_common.h"

static xt_value xt_node_stream_descriptor(const char *kind, const char *path) {
  xt_value stream = xt_object_new();
  xt_node_set(stream, "_kind", xt_string_from_cstr(kind));
  xt_node_set(stream, "_path", xt_string_from_cstr(path));
  return stream;
}

xt_value xt_node_create_read_stream(int32_t argc, xt_value *argv) {
  const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  if (!path) return xt_undefined();
  return xt_node_stream_descriptor("readStream", path);
}

xt_value xt_node_create_write_stream(int32_t argc, xt_value *argv) {
  const char *path = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : NULL;
  if (!path) return xt_undefined();
  return xt_node_stream_descriptor("writeStream", path);
}
