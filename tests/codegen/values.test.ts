
import { describe, expect, it } from "vitest";
import { booleanLiteral, i64, numberLiteral, XT_FALSE, XT_TRUE } from "../../src/codegen/values.js";

/** The 64-bit pattern of a double as LLVM would spell it. */
function bits(value: number): string {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value);
  const raw = (BigInt(view.getUint32(0)) << 32n) | BigInt(view.getUint32(4));
  return BigInt.asIntN(64, raw).toString();
}

describe("numberLiteral", () => {
  const values = [
    0,
    -0,
    1,
    -1,
    0.5,
    3.14159,
    1e100,
    -1e-100,
    1 / 3,
    Number.MIN_VALUE, // subnormal
    5e-324, // smallest subnormal
    2.2250738585072014e-308, // smallest normal
    1.9999999999999998, // rounding overflows the mantissa
    Number.MAX_VALUE,
    Infinity,
    -Infinity,
    NaN,
  ];

  for (const value of values) {
    it(`encodes ${Object.is(value, -0) ? "-0" : String(value)}`, () => {
      expect(numberLiteral(value)).toBe(bits(value));
    });
  }
});

describe("booleanLiteral", () => {
  it("encodes true and false with their tagged words", () => {
    expect(booleanLiteral(true)).toBe(i64(XT_TRUE));
    expect(booleanLiteral(false)).toBe(i64(XT_FALSE));
  });
});

describe("i64", () => {
  it("normalises unsigned patterns to signed decimals", () => {
    expect(i64(0xffffffffffffffffn)).toBe("-1");
    expect(i64(0x8000000000000000n)).toBe("-9223372036854775808");
  });
});
