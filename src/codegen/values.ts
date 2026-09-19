/**
 * The NaN-boxed value model shared by the compiler and the runtime.
 *
 * A JavaScript value is a 64-bit word. Doubles are stored unboxed; every other
 * value uses a tag in the high 16 bits plus a 48-bit payload. The compiler
 * emits these constants directly into LLVM IR, so this module (together with
 * runtime/rt.h) is the single source of truth for the representation.
 */

export const XT_TAG_MASK = 0xffff000000000000n;
export const XT_NUMBER_MASK = 0xfff8000000000000n;

export const XT_UNDEFINED = 0xfff8000000000000n;
export const XT_NULL = 0xfff9000000000000n;
export const XT_FALSE = 0xfffa000000000000n;
export const XT_TRUE = 0xfffb000000000000n;
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
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value);
  return i64(view.getBigInt64(0));
}

export function booleanLiteral(value: boolean): string {
  return i64(value ? XT_TRUE : XT_FALSE);
}
