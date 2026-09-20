/**
 * The NaN-boxed value model shared by the compiler and the runtime.
 *
 * A JavaScript value is a 64-bit word. Doubles are stored unboxed; every other
 * value uses a tag in the high 16 bits plus a 48-bit payload. The compiler
 * emits these constants directly into LLVM IR, so this module (together with
 * runtime/rt.h) is the single source of truth for the representation.
 *
 * The constants are written as pre-computed signed 64-bit decimal literals
 * rather than BigInt expressions, and `numberLiteral` derives the raw bits of a
 * double with plain arithmetic. That keeps this module free of BigInt and
 * typed-array APIs so the compiler can be compiled by itself on every platform.
 */

/** `0xffff000000000000` as a signed i64. */
export const XT_TAG_MASK = "-281474976710656";
/** `0xfff8000000000000` as a signed i64. */
export const XT_NUMBER_MASK = "-2251799813685248";

export const XT_UNDEFINED = "-2251799813685248"; // 0xfff8000000000000
export const XT_NULL = "-1970324836974592"; // 0xfff9000000000000
export const XT_FALSE = "-1688849860263936"; // 0xfffa000000000000
export const XT_TRUE = "-1407374883553280"; // 0xfffb000000000000
export const XT_STRING = "-1125899906842624"; // 0xfffc000000000000
export const XT_OBJECT = "-844424930131968"; // 0xfffd000000000000
export const XT_ARRAY = "-562949953421312"; // 0xfffe000000000000
export const XT_FUNCTION = "-281474976710656"; // 0xffff000000000000

/** Render a 64-bit constant the way LLVM expects signed i64 literals. */
export function i64(value: string): string {
  return value;
}

/** The raw 64 bits of an IEEE-754 double, as a signed i64 literal. */
export function numberLiteral(value: number): string {
  if (value !== value) return "9221120237041090560"; // 0x7ff8000000000000 (NaN)
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
  return signedDecimal(high >>> 0, low >>> 0);
}

export function booleanLiteral(value: boolean): string {
  return value ? XT_TRUE : XT_FALSE;
}

/** Two's-complement (high, low) 32-bit halves as a signed decimal string. */
function signedDecimal(high: number, low: number): string {
  if (high >= 0x80000000) {
    let h = (~high) >>> 0;
    let l = (~low) >>> 0;
    l = (l + 1) >>> 0;
    if (l === 0) h = (h + 1) >>> 0;
    return "-" + unsignedDecimal(h, l);
  }
  return unsignedDecimal(high, low);
}

/** Unsigned 64-bit `high * 2^32 + low` as a decimal string. */
function unsignedDecimal(high: number, low: number): string {
  const digits: number[] = [];
  let h = high;
  if (h === 0) digits.push(0);
  while (h > 0) {
    digits.push(h % 10);
    h = Math.floor(h / 10);
  }
  let carry = low;
  for (let i = 0; i < digits.length; i++) {
    const current = digits[i]! * 4294967296 + carry;
    digits[i] = current % 10;
    carry = Math.floor(current / 10);
  }
  while (carry > 0) {
    digits.push(carry % 10);
    carry = Math.floor(carry / 10);
  }
  let out = "";
  for (let i = digits.length - 1; i >= 0; i--) out += digits[i]!;
  return out;
}
