/**
 * Source text is a byte sequence.
 *
 * A compiled string is a sequence of UTF-8 bytes (`"e".length` is 1 for an
 * accent and 4 for an emoji - see doc/ai/language-support.md), and the compiler
 * is built by itself: there the self-hosted `readFileSync` hands the scanner a
 * file's own bytes, one per code unit, and the runtime has no UTF-8 decoder to
 * change that. A host whose strings are UTF-16 must be given the text in the
 * same shape, or the same program emits different bytes depending on which
 * generation of the compiler read it - which is how a non-ASCII literal ended
 * up double-encoded and broke the self-hosting fixpoint.
 *
 * `sourceTextBytes` is that shape: a source file's bytes, one code unit each.
 * The driver reads every source file through it, and codegen emits those bytes
 * unchanged.
 */

/**
 * The UTF-8 bytes of `text`, one code unit each. Text that is already a byte
 * sequence is kept as it is, so this is idempotent and safe to apply to a read
 * on either host.
 */
export function sourceTextBytes(text: string): string {
  if (!hasCharacter(text)) return startsWithBom(text) ? text.slice(3) : text;
  const encoded = encodeUtf8(text);
  return startsWithBom(encoded) ? encoded.slice(3) : encoded;
}

/** Encode text as UTF-8, spelled one code unit per byte. */
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

/** Whether any code unit is above one byte, which means text, not bytes. */
function hasCharacter(text: string): boolean {
  for (let index = 0; index < text.length; index++) {
    if (text.charCodeAt(index) > 0xff) return true;
  }
  return false;
}

/** Whether a decoded read started with a UTF-8 byte order mark. */
function startsWithBom(text: string): boolean {
  return text.charCodeAt(0) === 0xef && text.charCodeAt(1) === 0xbb && text.charCodeAt(2) === 0xbf;
}

/** Append the pending code units as one chunk of the byte string. */
function flushUnits(units: number[], chunks: string[]): void {
  if (units.length === 0) return;
  chunks.push(String.fromCharCode(...units));
  units.length = 0;
}