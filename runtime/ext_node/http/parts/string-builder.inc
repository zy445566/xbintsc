/* -- tiny string builder -------------------------------------------------- */

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} xt_http_buffer;

static void xt_http_buffer_init(xt_http_buffer *buffer) {
  buffer->capacity = 256;
  buffer->length = 0;
  buffer->data = (char *)malloc(buffer->capacity);
  buffer->data[0] = '\0';
}

static void xt_http_buffer_append(xt_http_buffer *buffer, const char *data, size_t length) {
  if (buffer->length + length + 1 > buffer->capacity) {
    while (buffer->length + length + 1 > buffer->capacity) buffer->capacity *= 2;
    buffer->data = (char *)realloc(buffer->data, buffer->capacity);
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
}

static void xt_http_buffer_free(xt_http_buffer *buffer) { free(buffer->data); }

/* Parse `name: value` header lines into an object with lower-cased keys. */
static xt_value xt_http_parse_headers(const char *headers, size_t length) {
  xt_value object = xt_object_new();
  size_t i = 0;
  while (i < length) {
    size_t lineEnd = i;
    while (lineEnd < length && headers[lineEnd] != '\n') lineEnd++;
    size_t lineLength = lineEnd - i;
    while (lineLength > 0 && (headers[i + lineLength - 1] == '\r' || headers[i + lineLength - 1] == ' ')) lineLength--;
    const char *colon = memchr(headers + i, ':', lineLength);
    if (colon) {
      size_t nameLength = (size_t)(colon - (headers + i));
      const char *value = colon + 1;
      size_t valueLength = lineLength - nameLength - 1;
      while (valueLength > 0 && *value == ' ') {
        value++;
        valueLength--;
      }
      char *name = (char *)malloc(nameLength + 1);
      for (size_t j = 0; j < nameLength; j++) {
        char c = headers[i + j];
        name[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
      }
      name[nameLength] = '\0';
      xt_object_set(object, xt_string_from_cstr(name), xt_string_new(value, valueLength));
      free(name);
    }
    i = lineEnd + 1;
  }
  return object;
}
