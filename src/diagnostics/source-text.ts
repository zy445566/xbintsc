/**
 * Source text is a byte sequence, one code unit per UTF-8 byte.
 *
 * A compiled string is a sequence of UTF-8 bytes (`"e".length` is 1 for an
 * accent and 4 for an emoji - see doc/ai/language-support.md), and the compiler
 * is built by itself: there the self-hosted `readFileSync` hands the scanner a
 * file's own bytes, one per code unit, and the runtime has no UTF-8 decoder to
 * change that. A host whose strings are UTF-16 must be given the text in the
 * same shape, or the same program emits different bytes depending on which
 * generation of the compiler read it.
 *
 * The conversion is explicit at the boundary:
 *
 * - `sourceTextBytes(hostText)` UTF-8 encodes text a host decoded for us (a
 *   `compileString` argument, a test fixture);
 * - `sourceTextFromBytes(fileBytes)` keeps a file's bytes as they are, which is
 *   the only correct reading for a source file on either host;
 * - `readSourceText(path)` reads a UTF-8 source file through the second one.
 *
 * There is deliberately no "does this string look like text?" heuristic: a
 * Latin-1-range character such as `é` has the same code unit (`0xE9`) whether
 * it was decoded from UTF-8 or is a raw byte, and guessing wrong emitted it as
 * a single invalid byte instead of the two-byte UTF-8 sequence.
 */

import { readFileSync } from "node:fs";

/**
 * The UTF-8 bytes of host text, one code unit each.
 *
 * Use this only for text a host decoded (a string literal in a test, a
 * `compileString` argument). A file's bytes must go through
 * {@link sourceTextFromBytes}; encoding those again doubles the non-ASCII
 * sequence and breaks the self-hosting fixpoint.
 */
export function sourceTextBytes(text: string): string {
  return stripBom(encodeUtf8(text));
}

/**
 * Internal source text from a file's raw bytes.
 *
 * `readFileSync` returns a `Buffer` on Node and a byte string (one code unit
 * per byte) in a self-hosted xbintsc; both arrive here. A UTF-8 BOM is dropped
 * so positions match the scanner's normalized text.
 */
export function sourceTextFromBytes(bytes: Uint8Array | string): string {
  return stripBom(typeof bytes === "string" ? bytes : bytesToString(bytes));
}

/** Read a UTF-8 source file as internal (byte-string) text. */
export function readSourceText(path: string): string {
  const data: Uint8Array | string = readFileSync(path);
  return sourceTextFromBytes(data);
}

/**
 * Decode internal byte text back to host text for display (diagnostics).
 *
 * Invalid sequences decode to U+FFFD, which is what a terminal would show for
 * a malformed byte; the parser never consumes the result.
 */
export function decodeSourceBytes(bytes: string): string {
  const chunks: string[] = [];
  const units: number[] = [];
  let index = 0;
  while (index < bytes.length) {
    const lead = bytes.charCodeAt(index) & 0xff;
    if (lead < 0x80) {
      units.push(lead);
      index += 1;
    } else if ((lead & 0xe0) === 0xc0) {
      const b1 = bytes.charCodeAt(index + 1);
      if ((b1 & 0xc0) === 0x80) {
        units.push(((lead & 0x1f) << 6) | (b1 & 0x3f));
        index += 2;
      } else {
        units.push(0xfffd);
        index += 1;
      }
    } else if ((lead & 0xf0) === 0xe0) {
      const b1 = bytes.charCodeAt(index + 1);
      const b2 = bytes.charCodeAt(index + 2);
      if ((b1 & 0xc0) === 0x80 && (b2 & 0xc0) === 0x80) {
        units.push(((lead & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f));
        index += 3;
      } else {
        units.push(0xfffd);
        index += 1;
      }
    } else if ((lead & 0xf8) === 0xf0) {
      const b1 = bytes.charCodeAt(index + 1);
      const b2 = bytes.charCodeAt(index + 2);
      const b3 = bytes.charCodeAt(index + 3);
      if ((b1 & 0xc0) === 0x80 && (b2 & 0xc0) === 0x80 && (b3 & 0xc0) === 0x80) {
        let code = ((lead & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
        if (code > 0x10ffff) code = 0xfffd;
        if (code >= 0x10000) {
          code -= 0x10000;
          units.push(0xd800 + (code >> 10), 0xdc00 + (code & 0x3ff));
        } else {
          units.push(code);
        }
        index += 4;
      } else {
        units.push(0xfffd);
        index += 1;
      }
    } else {
      units.push(0xfffd);
      index += 1;
    }
    if (units.length >= 512) flushUnits(units, chunks);
  }
  flushUnits(units, chunks);
  return chunks.join("");
}

/** Encode host text as a UTF-8 byte string. */
function encodeUtf8(text: string): string {
  const chunks: string[] = [];
  const units: number[] = [];
  for (let index = 0; index < text.length; index++) {
    let code = text.charCodeAt(index);
    if (code >= 0xd800 && code <= 0xdbff && index + 1 < text.length) {
      const low = text.charCodeAt(index + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        index++;
      }
    }
    if (code < 0x80) units.push(code);
    else if (code < 0x800) units.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    else if (code < 0x10000) {
      units.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    } else {
      units.push(
        0xf0 | (code >> 18),
        0x80 | ((code >> 12) & 0x3f),
        0x80 | ((code >> 6) & 0x3f),
        0x80 | (code & 0x3f),
      );
    }
    if (units.length >= 512) flushUnits(units, chunks);
  }
  flushUnits(units, chunks);
  return chunks.join("");
}

/** One code unit per byte of a file buffer. */
function bytesToString(bytes: Uint8Array): string {
  const chunks: string[] = [];
  const units: number[] = [];
  for (let index = 0; index < bytes.length; index++) {
    units.push(bytes[index]!);
    if (units.length >= 512) flushUnits(units, chunks);
  }
  flushUnits(units, chunks);
  return chunks.join("");
}

/** Whether the text starts with the UTF-8 encoding of a BOM. */
function stripBom(text: string): string {
  return text.charCodeAt(0) === 0xef && text.charCodeAt(1) === 0xbb && text.charCodeAt(2) === 0xbf
    ? text.slice(3)
    : text;
}

/** Append the pending code units as one chunk of the byte string. */
function flushUnits(units: number[], chunks: string[]): void {
  if (units.length === 0) return;
  chunks.push(String.fromCharCode(...units));
  units.length = 0;
}
