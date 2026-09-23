/*
 * `stream/promises` `pipeline` for xbintsc.
 *
 * Synchronous drain: read the source descriptor's file, apply each transform
 * (currently gzip), then write the destination descriptor's file. Returns an
 * already-resolved promise so `await pipeline(...)` works at the top level.
 *
 * Sources and destinations are the descriptor objects produced by
 * `fs.createReadStream` / `fs.createWriteStream`; a transform is the descriptor
 * produced by `zlib.createGzip`.
 */

#include "../node_common.h"

#include <string.h>

/* -- gzip (stored blocks) ------------------------------------------------- */

static uint32_t xt_pipeline_crc32(const unsigned char *data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)(-(int32_t)(crc & 1));
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

static void xt_pipeline_le32(unsigned char *out, uint32_t value) {
  out[0] = (unsigned char)(value & 0xff);
  out[1] = (unsigned char)((value >> 8) & 0xff);
  out[2] = (unsigned char)((value >> 16) & 0xff);
  out[3] = (unsigned char)((value >> 24) & 0xff);
}

/* Wrap `data` in a gzip container using DEFLATE stored blocks. */
static int xt_pipeline_gzip(const unsigned char *data, size_t length, unsigned char **out, size_t *outLength) {
  size_t blocks = length / 65535 + 1;
  size_t capacity = 10 + length + blocks * 5 + 8;
  unsigned char *buffer = (unsigned char *)malloc(capacity);
  if (!buffer) return 0;
  size_t pos = 0;
  buffer[pos++] = 0x1f;
  buffer[pos++] = 0x8b;
  buffer[pos++] = 0x08; /* CM: deflate */
  buffer[pos++] = 0x00; /* FLG */
  buffer[pos++] = 0x00; /* MTIME */
  buffer[pos++] = 0x00;
  buffer[pos++] = 0x00;
  buffer[pos++] = 0x00;
  buffer[pos++] = 0x00; /* XFL */
  buffer[pos++] = 0xff; /* OS: unknown */

  size_t offset = 0;
  do {
    size_t chunk = length - offset;
    if (chunk > 65535) chunk = 65535;
    int final = offset + chunk >= length;
    buffer[pos++] = (unsigned char)(final ? 0x01 : 0x00);
    buffer[pos++] = (unsigned char)(chunk & 0xff);
    buffer[pos++] = (unsigned char)((chunk >> 8) & 0xff);
    uint16_t inverse = (uint16_t)~chunk;
    buffer[pos++] = (unsigned char)(inverse & 0xff);
    buffer[pos++] = (unsigned char)((inverse >> 8) & 0xff);
    if (chunk > 0) memcpy(buffer + pos, data + offset, chunk);
    pos += chunk;
    offset += chunk;
  } while (offset < length);

  xt_pipeline_le32(buffer + pos, xt_pipeline_crc32(data, length));
  pos += 4;
  xt_pipeline_le32(buffer + pos, (uint32_t)length);
  pos += 4;
  *out = buffer;
  *outLength = pos;
  return 1;
}

/* -- descriptor helpers --------------------------------------------------- */

static const char *xt_pipeline_prop(xt_value object, const char *key) {
  if (!XT_IS_OBJECT(object)) return NULL;
  return xt_string_data(xt_to_string(xt_node_get(object, key)));
}

static int xt_pipeline_read_file(const char *path, unsigned char **data, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size < 0) {
    fclose(file);
    return 0;
  }
  unsigned char *buffer = (unsigned char *)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(file);
    return 0;
  }
  size_t read = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read] = 0;
  *data = buffer;
  *length = read;
  return 1;
}

static int xt_pipeline_write_file(const char *path, const unsigned char *data, size_t length) {
  FILE *file = fopen(path, "wb");
  if (!file) return 0;
  size_t written = length > 0 ? fwrite(data, 1, length, file) : 0;
  fclose(file);
  return written == length;
}

/* -- pipeline ------------------------------------------------------------- */

xt_value xt_node_stream_pipeline(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(xt_undefined());
  const char *sourcePath = xt_pipeline_prop(argv[0], "_path");
  const char *destPath = xt_pipeline_prop(argv[argc - 1], "_path");
  if (!sourcePath || !destPath) return xt_promise_resolve(xt_undefined());

  unsigned char *data = NULL;
  size_t length = 0;
  if (!xt_pipeline_read_file(sourcePath, &data, &length)) return xt_promise_resolve(xt_undefined());

  for (int32_t i = 1; i < argc - 1; i++) {
    const char *kind = xt_pipeline_prop(argv[i], "_kind");
    if (kind && strcmp(kind, "gzip") == 0) {
      unsigned char *compressed = NULL;
      size_t compressedLength = 0;
      if (xt_pipeline_gzip(data, length, &compressed, &compressedLength)) {
        free(data);
        data = compressed;
        length = compressedLength;
      }
    }
  }

  xt_pipeline_write_file(destPath, data, length);
  free(data);
  return xt_promise_resolve(xt_undefined());
}
