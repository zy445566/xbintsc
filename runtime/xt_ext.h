/*
 * xbintsc native-extension helper header.
 *
 * Include this from a C or C++ extension that xbintsc links into a generated
 * binary. It pulls in the runtime ABI (`rt.h`) and adds small helpers for the
 * common job of reading the `(argc, argv)` arguments and building result
 * values. In C++ everything is wrapped in `extern "C"` by `rt.h`, so a
 * definition like
 *
 *     XT_EXT_FN(mathx_add) {
 *       double a = xt_ext_number(argc, argv, 0, 0);
 *       double b = xt_ext_number(argc, argv, 1, 0);
 *       return xt_number(a + b);
 *     }
 *
 * exports a symbol callable from TypeScript (via a native manifest) without any
 * name mangling.
 *
 * Build the extension with the same clang that xbintsc uses, keeping the
 * runtime directory on the include path:
 *
 *     clang++ -O2 -fPIC -I<runtime> -c mathx.cpp -o mathx.o
 */
#ifndef XT_EXT_H
#define XT_EXT_H

#include <stddef.h>
#include <stdint.h>

#include "rt.h"

#ifdef __cplusplus
#define XT_EXT_FN(name) extern "C" xt_value name(int32_t argc, xt_value *argv)
#else
#define XT_EXT_FN(name) xt_value name(int32_t argc, xt_value *argv)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XT_EXT_FN(name) declares/defines an exported entry point with the signature
 * the code generator uses for builtins and module exports:
 *
 *     xt_value name(int32_t argc, xt_value *argv);
 *
 * In C++ it also gives the definition C linkage, so the symbol is never
 * name-mangled and the manifest can reference it directly.
 */

/** The `index`-th argument, or `undefined` when it was not supplied. */
static inline xt_value xt_ext_arg(int32_t argc, xt_value *argv, int32_t index) {
  if (argv == NULL || index < 0 || index >= argc) return XT_UNDEFINED;
  return argv[index];
}

/** The `index`-th argument coerced to a number, or `fallback` when absent. */
static inline double xt_ext_number(int32_t argc, xt_value *argv, int32_t index, double fallback) {
  xt_value value = xt_ext_arg(argc, argv, index);
  if (XT_IS_UNDEFINED(value)) return fallback;
  return xt_to_number(value);
}

/** The `index`-th argument coerced to a 32-bit integer. */
static inline int32_t xt_ext_int(int32_t argc, xt_value *argv, int32_t index, int32_t fallback) {
  return (int32_t)xt_ext_number(argc, argv, index, (double)fallback);
}

/** The `index`-th argument coerced to a boolean. */
static inline int xt_ext_bool(int32_t argc, xt_value *argv, int32_t index, int fallback) {
  xt_value value = xt_ext_arg(argc, argv, index);
  if (XT_IS_UNDEFINED(value)) return fallback;
  return xt_truthy(value);
}

/**
 * Borrow the UTF-8 bytes of the `index`-th argument when it is a string.
 * Returns `NULL` and stores 0 in `*length` otherwise. The pointer stays valid
 * for the lifetime of the value (the runtime arena never frees).
 */
static inline const char *xt_ext_string(int32_t argc, xt_value *argv, int32_t index, size_t *length) {
  xt_value value = xt_ext_arg(argc, argv, index);
  if (!XT_IS_STRING(value)) {
    if (length != NULL) *length = 0;
    return NULL;
  }
  if (length != NULL) *length = (size_t)xt_string_length_value(value);
  return xt_string_data(value);
}

/** Build a string value from bytes. */
static inline xt_value xt_ext_string_value(const char *data, size_t length) {
  return xt_string_new(data, length);
}

#ifdef __cplusplus
} /* extern "C" */

#include <string>

/** Convenience overload for `std::string` / string literals in C++. */
static inline xt_value xt_ext_string_from(const std::string &text) {
  return xt_string_new(text.data(), text.size());
}
#endif /* __cplusplus */

#endif /* XT_EXT_H */
