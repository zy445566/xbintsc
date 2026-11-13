/*
 * xbintsc runtime — the `Intl` global object.
 *
 * Only `Intl.Segmenter` is provided: the TUI subdivides strings into grapheme
 * clusters (terminal width, truncation) and words (cursor movement), and both
 * segmenters are constructed at module load. ICU is not linked, so the
 * segmentation is a documented approximation of UAX #29:
 *
 *   - grapheme: a code point plus the marks, variation selectors, emoji
 *     modifiers and ZWJ sequences that attach to it, and paired regional
 *     indicators (flags);
 *   - word: runs of whitespace, word characters and punctuation.
 *
 * `segment(text)` returns an **array** of `{ segment, index, isWordLike }`.
 * Node returns an iterable of the same records; every consumer in the
 * ecosystem spreads or iterates it, so the array is a compatible subset.
 *
 * Locale arguments are ignored, and `resolvedOptions()` reports `en`.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static xt_value xt_intl_object_value = 0;

/* -- code point classification -------------------------------------------- */

#define XT_CP_ZWJ 0x200D

static int xt_cp_is_mark(int32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
         (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
         (cp >= 0xfe20 && cp <= 0xfe2f) || (cp >= 0x0900 && cp <= 0x0903) ||
         (cp >= 0x093a && cp <= 0x094f) || (cp >= 0x0e31 && cp <= 0x0e3a) ||
         (cp >= 0x0e47 && cp <= 0x0e4e) || (cp >= 0x3099 && cp <= 0x309a);
}

static int xt_cp_is_variation_selector(int32_t cp) {
  return (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
}

static int xt_cp_is_emoji_modifier(int32_t cp) { return cp >= 0x1f3fb && cp <= 0x1f3ff; }

static int xt_cp_is_regional_indicator(int32_t cp) { return cp >= 0x1f1e6 && cp <= 0x1f1ff; }

static int xt_cp_is_whitespace(int32_t cp) {
  return cp == 0x20 || (cp >= 0x09 && cp <= 0x0d) || cp == 0xa0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 || cp == 0x202f ||
         cp == 0x205f || cp == 0x3000;
}

static int xt_cp_is_ascii_word(int32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

/* Non-ASCII letters are word characters for segmentation purposes; CJK,
 * Cyrillic, Greek and accents all land here. Ranges that are clearly symbols
 * (arrows, emoji, box drawing) are not. */
static int xt_cp_is_word(int32_t cp) {
  if (xt_cp_is_ascii_word(cp)) return 1;
  if (cp < 0x80) return 0;
  if (xt_cp_is_whitespace(cp) || xt_cp_is_mark(cp)) return 0;
  if (cp >= 0x1f000) return 0;
  if (cp >= 0x2190 && cp <= 0x2bff) return 0;
  if (cp >= 0x2500 && cp <= 0x259f) return 0;
  if (cp >= 0x3000 && cp <= 0x303f) return 0;
  return 1;
}

/* -- segment extraction ---------------------------------------------------- */

/** The text of `[start, end)` (UTF-16 units) as a JavaScript string. */
static xt_value xt_segment_text(xt_string *text, int32_t start, int32_t end) {
  size_t capacity = (size_t)(end - start + 1) * 4;
  char *buffer = (char *)malloc(capacity);
  if (!buffer) return xt_string_from_cstr("");
  size_t length = 0;
  for (int32_t index = start; index < end;) {
    int32_t code = xt_string_code_point_at(text, index);
    if (code < 0) break;
    index += code > 0xffff ? 2 : 1;
    length += xt_utf8_encode((uint32_t)code, buffer + length);
  }
  xt_value segment = xt_string_new(buffer, length);
  free(buffer);
  return segment;
}

static void xt_segment_push(xt_value array, xt_string *text, int32_t start, int32_t end, int word_like) {
  if (end <= start) return;
  xt_value record = xt_object_new();
  xt_object_set(record, xt_string_from_cstr("segment"), xt_segment_text(text, start, end));
  xt_object_set(record, xt_string_from_cstr("index"), xt_number((double)start));
  xt_object_set(record, xt_string_from_cstr("isWordLike"), xt_bool(word_like));
  xt_array_push(array, record);
}

/* True when `code` attaches to the preceding cluster instead of starting one. */
static int xt_cp_extends_cluster(int32_t code) {
  return xt_cp_is_mark(code) || xt_cp_is_variation_selector(code) || xt_cp_is_emoji_modifier(code) ||
         code == XT_CP_ZWJ;
}

static xt_value xt_segment_graphemes(xt_string *text) {
  xt_value array = xt_array_new(0, NULL);
  int32_t units = (int32_t)xt_string_utf16_length(text);
  int32_t start = 0;
  int32_t index = 0;
  int32_t cluster_codepoints = 0;
  int32_t last_code = 0;
  while (index < units) {
    int32_t code = xt_string_code_point_at(text, index);
    if (code < 0) break;
    int32_t width = code > 0xffff ? 2 : 1;
    int extends = index > start && xt_cp_extends_cluster(code);
    /* A pair of regional indicators is one flag. */
    if (!extends && index > start && xt_cp_is_regional_indicator(code) &&
        xt_cp_is_regional_indicator(last_code) && cluster_codepoints == 1) {
      extends = 1;
    }
    /* A ZWJ consumes the code point that follows it (emoji sequences). */
    if (!extends && index > start && last_code == XT_CP_ZWJ) extends = 1;
    if (!extends && index > start) {
      xt_segment_push(array, text, start, index, 0);
      start = index;
      cluster_codepoints = 0;
    }
    cluster_codepoints++;
    last_code = code;
    index += width;
  }
  xt_segment_push(array, text, start, units, 0);
  return array;
}

static xt_value xt_segment_words(xt_string *text) {
  xt_value array = xt_array_new(0, NULL);
  int32_t units = (int32_t)xt_string_utf16_length(text);
  int32_t start = 0;
  int32_t index = 0;
  int class_of_run = -1; /* 0 punctuation, 1 word, 2 whitespace */
  while (index < units) {
    int32_t code = xt_string_code_point_at(text, index);
    if (code < 0) break;
    int32_t width = code > 0xffff ? 2 : 1;
    int kind = xt_cp_is_whitespace(code) ? 2 : (xt_cp_is_word(code) ? 1 : 0);
    /* Marks continue whatever run they land in. */
    if (xt_cp_is_mark(code) && class_of_run >= 0) kind = class_of_run;
    if (class_of_run >= 0 && kind != class_of_run) {
      xt_segment_push(array, text, start, index, class_of_run == 1);
      start = index;
    }
    class_of_run = kind;
    index += width;
  }
  xt_segment_push(array, text, start, units, class_of_run == 1);
  return array;
}

/* -- segmenter objects ---------------------------------------------------- */

static const char *xt_segmenter_granularity(xt_value segmenter) {
  xt_value value = xt_object_get_cstr(segmenter, "__granularity");
  const char *granularity = XT_IS_STRING(value) ? xt_string_data(value) : NULL;
  return granularity ? granularity : "grapheme";
}

static xt_value xt_intl_segment(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *granularity = xt_segmenter_granularity(thisValue);
  xt_value input = xt_arg(argc, argv, 0);
  xt_string *text = xt_as_string(xt_to_string(input));
  if (!text) return xt_array_new(0, NULL);
  if (strcmp(granularity, "word") == 0) return xt_segment_words(text);
  if (strcmp(granularity, "sentence") == 0) return xt_segment_words(text);
  return xt_segment_graphemes(text);
}

static xt_value xt_intl_resolved_options(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value options = xt_object_new();
  xt_object_set(options, xt_string_from_cstr("locale"), xt_string_from_cstr("en"));
  xt_object_set(options, xt_string_from_cstr("granularity"),
                xt_string_from_cstr(xt_segmenter_granularity(thisValue)));
  return options;
}

/* `new Intl.Segmenter(locale, { granularity })` returns the segmenter object
 * itself; `xt_new` keeps it because a constructor that returns an object wins. */
static xt_value xt_intl_segmenter_ctor(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  const char *granularity = "grapheme";
  xt_value options = xt_arg(argc, argv, 1);
  if (XT_IS_OBJECT(options)) {
    xt_value requested = xt_object_get_cstr(options, "granularity");
    if (XT_IS_STRING(requested)) granularity = xt_string_data(requested);
  }
  xt_value segmenter = xt_object_new();
  xt_object_set(segmenter, xt_string_from_cstr("__granularity"), xt_string_from_cstr(granularity));
  xt_value segment = xt_closure_new((void *)xt_intl_segment, 0, NULL);
  xt_function_set_metadata(segment, xt_string_from_cstr("segment"), 1);
  xt_object_set(segmenter, xt_string_from_cstr("segment"), segment);
  xt_value resolved = xt_closure_new((void *)xt_intl_resolved_options, 0, NULL);
  xt_function_set_metadata(resolved, xt_string_from_cstr("resolvedOptions"), 0);
  xt_object_set(segmenter, xt_string_from_cstr("resolvedOptions"), resolved);
  return segmenter;
}

xt_value xt_intl_object(void) {
  xt_gc_add_root(&xt_intl_object_value);
  if (xt_intl_object_value) return xt_intl_object_value;
  xt_value intl = xt_object_new();
  xt_value segmenter = xt_closure_new((void *)xt_intl_segmenter_ctor, 0, NULL);
  xt_function_set_metadata(segmenter, xt_string_from_cstr("Segmenter"), 0);
  xt_object_set(intl, xt_string_from_cstr("Segmenter"), segmenter);
  xt_intl_object_value = intl;
  return xt_intl_object_value;
}
