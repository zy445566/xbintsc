// A C++ extension for xbintsc.
//
// Everything exported to TypeScript goes through `XT_EXT_FN`, which expands to
// the runtime ABI `xt_value name(int32_t argc, xt_value *argv)` with C linkage
// (the surrounding `extern "C"` comes from `runtime/rt.h`). Internally the code
// is ordinary modern C++: templates, the standard library, `std::string`, ...
//
// Build it into a static archive (see `build.sh`) and register the archive with
// xbintsc through `xbintsc.manifest.json`.

#include "xt_ext.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

// A small C++ template used by the exported helpers below, to show that the
// extension side is unconstrained C++ — only the boundary is C.
template <typename T>
T clamp_to(T value, T low, T high) {
  if (high < low) std::swap(low, high);
  return std::min(std::max(value, low), high);
}

}  // namespace

// add(a: number, b: number): number
XT_EXT_FN(mathx_add) {
  const double a = xt_ext_number(argc, argv, 0, 0.0);
  const double b = xt_ext_number(argc, argv, 1, 0.0);
  return xt_number(a + b);
}

// fib(n: number): number
XT_EXT_FN(mathx_fib) {
  const int32_t n = xt_ext_int(argc, argv, 0, 0);
  if (n <= 1) return xt_number(n < 0 ? 0 : n);
  double previous = 0.0;
  double current = 1.0;
  for (int32_t i = 2; i <= n; i++) {
    const double next = previous + current;
    previous = current;
    current = next;
  }
  return xt_number(current);
}

// fibSequence(count: number): number[]
XT_EXT_FN(mathx_fib_sequence) {
  int32_t count = xt_ext_int(argc, argv, 0, 0);
  if (count < 0) count = 0;
  std::vector<xt_value> items;
  items.reserve(static_cast<size_t>(count));
  double previous = 0.0;
  double current = 1.0;
  for (int32_t i = 0; i < count; i++) {
    items.push_back(xt_number(previous));
    const double next = previous + current;
    previous = current;
    current = next;
  }
  return xt_array_new(static_cast<int32_t>(items.size()), items.data());
}

// reverse(text: string): string
XT_EXT_FN(mathx_reverse) {
  size_t length = 0;
  const char *text = xt_ext_string(argc, argv, 0, &length);
  if (text == NULL) return xt_ext_string_value("", 0);
  std::string reversed(text, length);
  std::reverse(reversed.begin(), reversed.end());
  return xt_ext_string_from(reversed);
}

// clamp(value: number, low: number, high: number): number
XT_EXT_FN(mathx_clamp) {
  const double value = xt_ext_number(argc, argv, 0, 0.0);
  const double low = xt_ext_number(argc, argv, 1, 0.0);
  const double high = xt_ext_number(argc, argv, 2, 0.0);
  return xt_number(clamp_to(value, low, high));
}
