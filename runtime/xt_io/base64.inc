/* ------------------------------------------------------------------------- */
/* base64 globals (btoa/atob)                                                */
/* ------------------------------------------------------------------------- */

static const char xt_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

xt_value xt_btoa(int32_t argc, xt_value *argv) {
  const char *input = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  if (!input) input = "";
  size_t length = strlen(input);
  size_t outLength = ((length + 2) / 3) * 4;
  char *buffer = (char *)malloc(outLength + 1);
  if (!buffer) return xt_string_from_cstr("");
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    unsigned int n = ((unsigned char)input[i] << 16) | ((unsigned char)input[i + 1] << 8) |
                     (unsigned char)input[i + 2];
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    unsigned int n = (unsigned char)input[i] << 16;
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = '=';
    buffer[j++] = '=';
  } else if (length - i == 2) {
    unsigned int n = ((unsigned char)input[i] << 16) | ((unsigned char)input[i + 1] << 8);
    buffer[j++] = xt_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = '=';
  }
  buffer[j] = 0;
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

static int xt_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

xt_value xt_atob(int32_t argc, xt_value *argv) {
  const char *input = argc > 0 ? xt_string_data(xt_to_string(argv[0])) : "";
  if (!input) input = "";
  size_t length = strlen(input);
  char *buffer = (char *)malloc(length / 4 * 3 + 1);
  if (!buffer) return xt_string_from_cstr("");
  size_t out = 0;
  int values[4];
  int count = 0;
  for (size_t i = 0; i < length; i++) {
    char c = input[i];
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      if (c == '=') break;
      continue;
    }
    int value = xt_base64_value(c);
    if (value < 0) continue;
    values[count++] = value;
    if (count == 4) {
      buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
      buffer[out++] = (char)(((values[1] & 15) << 4) | (values[2] >> 2));
      buffer[out++] = (char)(((values[2] & 3) << 6) | values[3]);
      count = 0;
    }
  }
  if (count == 2) {
    buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
  } else if (count == 3) {
    buffer[out++] = (char)((values[0] << 2) | (values[1] >> 4));
    buffer[out++] = (char)(((values[1] & 15) << 4) | (values[2] >> 2));
  }
  xt_value result = xt_string_new(buffer, out);
  free(buffer);
  return result;
}
