// Demonstrates a C++ extension linked into an xbintsc binary.
//
// Build the C++ side first:
//   ./examples/extensions/cpp/build.sh
//
// Then compile and run (from the repository root):
//   npx tsx src/cli/main.ts run examples/extensions/cpp/demo.ts \
//     --ext-native examples/extensions/cpp/xbintsc.manifest.json
//
// `mathx` is provided by the C++ static library; `cppClamp` is a global
// function bound directly to a C++ symbol.

import { add, fib, fibSequence, reverse } from "mathx";

console.log("add(2, 3)             =", add(2, 3));
console.log("fib(10)               =", fib(10));
console.log("fibSequence(8)        =", JSON.stringify(fibSequence(8)));
console.log("reverse('xbintsc')    =", reverse("xbintsc"));
console.log("cppClamp(12, 0, 10)   =", cppClamp(12, 0, 10));
