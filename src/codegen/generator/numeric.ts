/**
 * Inline fast paths for numeric operators.
 *
 * Every JavaScript operator used to be a runtime call (`xt_add`, `xt_lt`, …).
 * That is correct but expensive in numeric code: a `for` loop paid three calls
 * per iteration, and clang cannot inline across the generated IR / C runtime
 * boundary. For two unboxed doubles the operation is just a hardware
 * instruction, so emit that inline and keep the runtime call as the cold path
 * for every other combination (strings, BigInt, coercion, …).
 *
 * Doubles are stored bit-for-bit in `xt_value`, so the tag check is an integer
 * mask and the operation is `bitcast` → `fadd`/`fcmp` → `bitcast`.
 */

import { i64, XT_FALSE, XT_NUMBER_MASK, XT_TRUE } from "../values.js";
import type { Generator } from "./generator.js";

export interface NumericMethods {
  /**
   * Emit `left <operator> right`, using an inline double fast path when both
   * operands turn out to be numbers and {@link allowFastPath} is set.
   */
  emitBinaryOperation(
    this: Generator,
    left: string,
    right: string,
    operator: string,
    runtime: string,
    allowFastPath?: boolean,
  ): string;
  emitNumericBinary(this: Generator, left: string, right: string, instruction: string, runtime: string): string;
  emitNumericComparison(this: Generator, left: string, right: string, predicate: string, runtime: string): string;
  /** Emit an i1 SSA value that is true when `value` is an unboxed double. */
  isNumberValue(this: Generator, value: string): string;
}

/** Canonical quiet NaN; the runtime uses the same pattern in `xt_from_double`. */
const CANONICAL_NAN = 0x7ff8000000000000n;

const NUMERIC_ARITHMETIC: Record<string, string | undefined> = {
  "+": "fadd",
  "-": "fsub",
  "*": "fmul",
  "/": "fdiv",
  "%": "frem",
};

const NUMERIC_COMPARISON: Record<string, string | undefined> = {
  "<": "olt",
  "<=": "ole",
  ">": "ogt",
  ">=": "oge",
  "==": "oeq",
  "!=": "une",
  "===": "oeq",
  "!==": "une",
};

export const numericMethods: NumericMethods = {
  emitBinaryOperation(left, right, operator, runtime, allowFastPath = true) {
    if (allowFastPath) {
      const instruction = NUMERIC_ARITHMETIC[operator];
      if (instruction) return this.emitNumericBinary(left, right, instruction, runtime);
      const predicate = NUMERIC_COMPARISON[operator];
      if (predicate) return this.emitNumericComparison(left, right, predicate, runtime);
    }
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${runtime}(i64 ${left}, i64 ${right})`);
    return result;
  },

  /** Emit an i1 SSA value that is true when `value` is an unboxed double. */
  isNumberValue(value: string): string {
    const masked = this.reg();
    this.emit(`  ${masked} = and i64 ${value}, ${i64(XT_NUMBER_MASK)}`);
    const result = this.reg();
    this.emit(`  ${result} = icmp ne i64 ${masked}, ${i64(XT_NUMBER_MASK)}`);
    return result;
  },

  emitNumericBinary(left, right, instruction, runtime) {
    const result = this.alloca();
    const leftIsNumber = this.isNumberValue(left);
    const rightIsNumber = this.isNumberValue(right);
    const bothNumbers = this.reg();
    this.emit(`  ${bothNumbers} = and i1 ${leftIsNumber}, ${rightIsNumber}`);
    const fast = this.label("num.fast");
    const slow = this.label("num.slow");
    const end = this.label("num.end");
    this.terminate(`br i1 ${bothNumbers}, label %${fast}, label %${slow}`);

    this.startBlock(fast);
    const leftDouble = this.reg();
    this.emit(`  ${leftDouble} = bitcast i64 ${left} to double`);
    const rightDouble = this.reg();
    this.emit(`  ${rightDouble} = bitcast i64 ${right} to double`);
    const computed = this.reg();
    this.emit(`  ${computed} = ${instruction} double ${leftDouble}, ${rightDouble}`);
    // Hardware NaNs carry many payloads; the value model only reserves the
    // canonical quiet NaN, and `-NaN` would otherwise collide with XT_UNDEFINED.
    const bits = this.reg();
    this.emit(`  ${bits} = bitcast double ${computed} to i64`);
    const unordered = this.reg();
    this.emit(`  ${unordered} = fcmp uno double ${computed}, ${computed}`);
    const canonical = this.reg();
    this.emit(`  ${canonical} = select i1 ${unordered}, i64 ${i64(CANONICAL_NAN)}, i64 ${bits}`);
    this.emit(`  store i64 ${canonical}, i64* ${result}`);
    this.terminate(`br label %${end}`);

    this.startBlock(slow);
    const called = this.reg();
    this.emit(`  ${called} = call i64 @${runtime}(i64 ${left}, i64 ${right})`);
    this.emit(`  store i64 ${called}, i64* ${result}`);
    this.terminate(`br label %${end}`);

    this.startBlock(end);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    return value;
  },

  emitNumericComparison(left, right, predicate, runtime) {
    const result = this.alloca();
    const leftIsNumber = this.isNumberValue(left);
    const rightIsNumber = this.isNumberValue(right);
    const bothNumbers = this.reg();
    this.emit(`  ${bothNumbers} = and i1 ${leftIsNumber}, ${rightIsNumber}`);
    const fast = this.label("cmp.fast");
    const slow = this.label("cmp.slow");
    const end = this.label("cmp.end");
    this.terminate(`br i1 ${bothNumbers}, label %${fast}, label %${slow}`);

    this.startBlock(fast);
    const leftDouble = this.reg();
    this.emit(`  ${leftDouble} = bitcast i64 ${left} to double`);
    const rightDouble = this.reg();
    this.emit(`  ${rightDouble} = bitcast i64 ${right} to double`);
    // Ordered comparisons are false for NaN and unordered `!=` is true, which
    // is exactly ECMAScript's behaviour for two numbers.
    const compared = this.reg();
    this.emit(`  ${compared} = fcmp ${predicate} double ${leftDouble}, ${rightDouble}`);
    const value = this.reg();
    this.emit(`  ${value} = select i1 ${compared}, i64 ${i64(XT_TRUE)}, i64 ${i64(XT_FALSE)}`);
    this.emit(`  store i64 ${value}, i64* ${result}`);
    this.terminate(`br label %${end}`);

    this.startBlock(slow);
    const called = this.reg();
    this.emit(`  ${called} = call i64 @${runtime}(i64 ${left}, i64 ${right})`);
    this.emit(`  store i64 ${called}, i64* ${result}`);
    this.terminate(`br label %${end}`);

    this.startBlock(end);
    const merged = this.reg();
    this.emit(`  ${merged} = load i64, i64* ${result}`);
    return merged;
  },
};
