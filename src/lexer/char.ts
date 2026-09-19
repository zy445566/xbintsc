/**
 * Character classification used by the scanner.
 *
 * Keeping the code-point constants and the `is*` predicates in their own module
 * keeps the scanner focused on tokenization and makes the low-level rules easy
 * to test in isolation.
 */

export const enum Char {
  Null = 0,
  Tab = 9,
  LineFeed = 10,
  VerticalTab = 11,
  FormFeed = 12,
  CarriageReturn = 13,
  Space = 32,
  Exclamation = 33,
  DoubleQuote = 34,
  Hash = 35,
  Dollar = 36,
  Percent = 37,
  Ampersand = 38,
  SingleQuote = 39,
  OpenParen = 40,
  CloseParen = 41,
  Asterisk = 42,
  Plus = 43,
  Comma = 44,
  Minus = 45,
  Dot = 46,
  Slash = 47,
  Zero = 48,
  Nine = 57,
  Colon = 58,
  Semicolon = 59,
  LessThan = 60,
  Equals = 61,
  GreaterThan = 62,
  Question = 63,
  At = 64,
  UpperA = 65,
  UpperB = 66,
  UpperE = 69,
  UpperF = 70,
  UpperO = 79,
  UpperX = 88,
  UpperZ = 90,
  OpenBracket = 91,
  Backslash = 92,
  CloseBracket = 93,
  Caret = 94,
  Underscore = 95,
  Backtick = 96,
  LowerA = 97,
  LowerB = 98,
  LowerE = 101,
  LowerF = 102,
  LowerN = 110,
  LowerO = 111,
  LowerR = 114,
  LowerT = 116,
  LowerU = 117,
  LowerX = 120,
  LowerZ = 122,
  OpenBrace = 123,
  Bar = 124,
  CloseBrace = 125,
  Tilde = 126,
  NonBreakingSpace = 0xa0,
  LineSeparator = 0x2028,
  ParagraphSeparator = 0x2029,
  ByteOrderMark = 0xfeff,
}

export function isLineBreak(ch: number): boolean {
  return ch === Char.LineFeed || ch === Char.CarriageReturn || ch === Char.LineSeparator || ch === Char.ParagraphSeparator;
}

const WHITESPACE_UNICODE = new Set<number>([
  0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200a, 0x202f, 0x205f,
  0x3000,
]);

export function isWhiteSpace(ch: number): boolean {
  return (
    ch === Char.Space ||
    ch === Char.Tab ||
    ch === Char.VerticalTab ||
    ch === Char.FormFeed ||
    ch === Char.NonBreakingSpace ||
    ch === Char.ByteOrderMark ||
    (ch > 0x1680 && WHITESPACE_UNICODE.has(ch))
  );
}

export function isDigit(ch: number): boolean {
  return ch >= Char.Zero && ch <= Char.Nine;
}

export function isHexDigit(ch: number): boolean {
  return isDigit(ch) || (ch >= Char.LowerA && ch <= Char.LowerF) || (ch >= Char.UpperA && ch <= Char.UpperF);
}

export function isIdentifierStart(ch: number): boolean {
  return (
    (ch >= Char.LowerA && ch <= Char.LowerZ) ||
    (ch >= Char.UpperA && ch <= Char.UpperZ) ||
    ch === Char.Dollar ||
    ch === Char.Underscore ||
    ch > 0x7f
  );
}

export function isIdentifierPart(ch: number): boolean {
  return isIdentifierStart(ch) || isDigit(ch) || ch === Char.Backslash || ch > 0x7f;
}
