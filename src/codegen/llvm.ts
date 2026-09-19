/**
 * LLVM IR code generation.
 *
 * The generator lowers the bound AST to textual LLVM IR. It follows three
 * rules that keep the output simple and correct without a register allocator
 * of our own:
 *
 *   1. Every value crossing a statement or block boundary lives in an
 *      `alloca` (emitted in the entry block so LLVM's mem2reg can promote it).
 *   2. Control flow is expressed with explicit basic blocks; conditionals and
 *      short-circuit operators materialise into a temporary slot instead of
 *      `phi` nodes, which the optimizer folds away.
 *   3. All JavaScript semantics (addition with coercion, comparisons, member
 *      lookup, ...) are delegated to the C runtime through `@xt_*` calls.
 *
 * Later pipeline stages (optimization passes, object emission) live in the
 * driver; this module only produces IR text.
 *
 * The implementation is split by function under `./generator/`; this module is
 * the stable entry point.
 */

export { generate, type CodegenOptions, type CodegenResult } from "./generator/generator.js";
