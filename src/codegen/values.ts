/**
 * The NaN-boxed value model shared by the compiler and the runtime.
 *
 * A JavaScript value is a 64-bit word. Doubles are stored unboxed; every other
 * value uses a tag in the high 16 bits plus a 48-bit payload. The compiler
 * emits these constants directly into LLVM IR, so this module (together with
 * runtime/rt.h) is the single source of truth for the representation.
 *
 * BigInts are available both to the host (Node) running the compiler and to
 * the compiler once self-hosted, so the constants below are written as BigInt
 * literals and `numberLiteral` derives a double's raw bits with BigInt
 * arithmetic. `i64` normalises any 64-bit pattern to the signed decimal form
 * LLVM expects.
 */

export const XT_TAG_MASK = 0xffff000000000000n;
export const XT_NUMBER_MASK = 0xfff8000000000000n;

export const XT_UNDEFINED = 0xfff8000000000000n;
export const XT_NULL = 0xfff9000000000000n;
export const XT_FALSE = 0xfffa000000000000n;
export const XT_TRUE = 0xfffa000000000001n;
export const XT_BIGINT = 0xfffb000000000000n;
export const XT_STRING = 0xfffc000000000000n;
export const XT_OBJECT = 0xfffd000000000000n;
export const XT_ARRAY = 0xfffe000000000000n;
export const XT_FUNCTION = 0xffff000000000000n;

/** Render a 64-bit constant the way LLVM expects signed i64 literals. */
export function i64(value: bigint): string {
  return BigInt.asIntN(64, value).toString();
}

/** The raw 64 bits of an IEEE-754 double, as a signed i64 literal. */
export function numberLiteral(value: number): string {
  if (value !== value) return i64(0x7ff8000000000000n);
  const negative = value < 0 || (value === 0 && 1 / value < 0);
  const abs = negative ? -value : value;
  let high: number;
  let low: number;
  if (abs === 0) {
    high = 0;
    low = 0;
  } else if (abs === Infinity) {
    high = 0x7ff00000;
    low = 0;
  } else {
    let exponent = Math.floor(Math.log2(abs));
    while (exponent < 1023 && Math.pow(2, exponent + 1) <= abs) exponent++;
    while (exponent > -1022 && Math.pow(2, exponent) > abs) exponent--;
    let mantissa = Math.round((abs / Math.pow(2, exponent) - 1) * 4503599627370496);
    let biased = exponent + 1023;
    if (biased <= 0) {
      mantissa = Math.round((abs / Math.pow(2, -1022)) * 4503599627370496);
      biased = 0;
    } else if (mantissa >= 4503599627370496) {
      mantissa = 0;
      biased += 1;
    }
    high = biased * 0x100000 + Math.floor(mantissa / 0x100000000);
    low = mantissa % 0x100000000;
  }
  if (negative) high += 0x80000000;
  const bits = (BigInt(high >>> 0) << 32n) | BigInt(low >>> 0);
  return i64(bits);
}

export function booleanLiteral(value: boolean): string {
  return i64(value ? XT_TRUE : XT_FALSE);
}
