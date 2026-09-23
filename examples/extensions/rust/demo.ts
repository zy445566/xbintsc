// Demonstrates a Rust extension linked into an xbintsc binary.
//
// Build the Rust side first:
//   ./examples/extensions/rust/build.sh
//
// Then compile and run (from the repository root):
//   npx tsx src/cli/main.ts run examples/extensions/rust/demo.ts \
//     --ext-native examples/extensions/rust/xbintsc.manifest.json
//
// `mathx` is provided by the Rust static library; `rustClamp` is a global
// function bound directly to a Rust symbol.

import { add, fib, fibSequence, reverse } from "mathx";

console.log("add(2, 3)             =", add(2, 3));
console.log("fib(10)               =", fib(10));
console.log("fibSequence(8)        =", JSON.stringify(fibSequence(8)));
console.log("reverse('xbintsc')    =", reverse("xbintsc"));
console.log("rustClamp(12, 0, 10)  =", rustClamp(12, 0, 10));
