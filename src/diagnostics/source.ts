/**
 * Source file abstraction: owns the raw byte text plus a line-offset index so
 * that positions (byte offsets) can be mapped back to line/column pairs for
 * diagnostics and for incremental-compilation fingerprints.
 *
 * `text` is the compiler's internal representation: UTF-8 bytes, one code unit
 * per byte (see `source-text.ts`). Offsets in the AST and diagnostics are
 * indexes into that byte string. `positionAt` reports the line and the column
 * a reader sees, so the column is counted in UTF-16 code units after decoding
 * the line prefix; the raw byte column would drift from an editor's column as
 * soon as a line contains non-ASCII text.
 */

import { decodeSourceBytes } from "./source-text.js";

export interface LineAndColumn {
  /** 1-based line number. */
  readonly line: number;
  /** 1-based column number (UTF-16 code units, as an editor counts). */
  readonly column: number;
}

/**
 * A text position. `offset` is a byte index into the source's internal UTF-8
 * representation (`SourceFile.text`), not a UTF-16 code-unit index.
 */
export interface TextPosition {
  readonly offset: number;
}

export interface TextRange {
  readonly start: number;
  readonly end: number;
}

export class SourceFile {
  readonly fileName: string;
  /** Internal UTF-8 byte text: one code unit per byte. */
  readonly text: string;
  /** Content hash used by the incremental compiler. */
  readonly hash: string;
  private lineStarts: number[] | undefined;

  constructor(fileName: string, text: string) {
    this.fileName = fileName;
    // Normalize BOM and line endings so downstream stages only deal with "\n".
    // A file read through `readSourceText` already dropped the byte BOM; a
    // host text argument may still carry the U+FEFF code unit.
    const normalized = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
    this.text = normalized.replace(/\r\n?/g, "\n");
    this.hash = hashText(this.text);
  }

  get length(): number {
    return this.text.length;
  }

  private getLineStarts(): number[] {
    if (this.lineStarts === undefined) {
      const starts = [0];
      const text = this.text;
      for (let i = 0; i < text.length; i++) {
        if (text.charCodeAt(i) === 10 /* \n */) starts.push(i + 1);
      }
      this.lineStarts = starts;
    }
    return this.lineStarts;
  }

  positionAt(offset: number): LineAndColumn {
    const clamped = Math.max(0, Math.min(offset, this.text.length));
    const starts = this.getLineStarts();
    // Binary search for the greatest line start <= clamped.
    let lo = 0;
    let hi = starts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (starts[mid]! <= clamped) lo = mid;
      else hi = mid - 1;
    }
    const prefix = this.text.slice(starts[lo]!, clamped);
    return { line: lo + 1, column: decodeSourceBytes(prefix).length + 1 };
  }

  /** The display text of a 1-based line, without its trailing newline. */
  lineText(line: number): string {
    const starts = this.getLineStarts();
    if (line < 1 || line > starts.length) return "";
    const start = starts[line - 1]!;
    const next = line < starts.length ? starts[line]! - 1 : this.text.length;
    return decodeSourceBytes(this.text.slice(start, next));
  }

  /** Extract the raw source text covered by a range (internal byte text). */
  slice(range: TextRange): string {
    return this.text.slice(range.start, range.end);
  }
}

/**
 * FNV-1a 64-bit content hash rendered as a hex string. Small, dependency free
 * and stable across platforms which is exactly what the incremental cache
 * needs. Collision resistance is not a security property here.
 */
export function hashText(text: string, seed = 0xcbf29ce484222325n): string {
  let hash = seed;
  const prime = 0x100000001b3n;
  const mask = 0xffffffffffffffffn;
  for (let i = 0; i < text.length; i++) {
    hash ^= BigInt(text.charCodeAt(i));
    hash = (hash * prime) & mask;
  }
  return hash.toString(16).padStart(16, "0");
}

/** Combine several hashes/strings into one stable fingerprint. */
export function combineHashes(parts: readonly string[]): string {
  return hashText(parts.join("\u0000"));
}
