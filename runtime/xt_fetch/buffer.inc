/* -- growable byte buffer ------------------------------------------------- */

/*
 * Fetch needs to accumulate raw response bytes before the headers and body can
 * be parsed. A small growable buffer keeps that logic in one place; it is
 * always NUL-terminated so it can also back a JS string, but the explicit
 * `length` is authoritative because a response body may contain NUL bytes.
 */
typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} xt_fetch_buffer;

static void xt_fetch_buffer_init(xt_fetch_buffer *buffer) {
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static void xt_fetch_buffer_free(xt_fetch_buffer *buffer) {
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static void xt_fetch_buffer_append(xt_fetch_buffer *buffer, const char *data, size_t length) {
  if (length == 0) {
    if (!buffer->data) {
      buffer->capacity = 1;
      buffer->data = (char *)malloc(1);
      if (!buffer->data) abort();
      buffer->data[0] = '\0';
    }
    return;
  }
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < buffer->length + length + 1) capacity *= 2;
    char *grown = (char *)realloc(buffer->data, capacity);
    if (!grown) abort();
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
}

static void xt_fetch_buffer_append_cstr(xt_fetch_buffer *buffer, const char *text) {
  xt_fetch_buffer_append(buffer, text, strlen(text));
}

/* -- string helpers ------------------------------------------------------- */

static void xt_fetch_lowercase(const char *input, char *output, size_t size) {
  size_t i = 0;
  for (; input[i] && i + 1 < size; i++) {
    char c = input[i];
    output[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  output[i] = '\0';
}
