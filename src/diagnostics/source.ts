/**
 * Source file abstraction: owns the raw text plus a line-offset index so that
 * positions (character offsets) can be mapped back to line/column pairs for
 * diagnostics and for incremental-compilation fingerprints.
 */

export interface LineAndColumn {
  /** 1-based line number. */
  readonly line: number;
  /** 1-based column number (UTF-16 code units). */
  readonly column: number;
}

/**
 * A text position. `offset` is a UTF-16 code-unit index into the source text,
 * matching how the JavaScript/TypeScript toolchain measures positions.
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
  readonly text: string;
  /** Content hash used by the incremental compiler. */
  readonly hash: string;
  private lineStarts: number[] | undefined;

  constructor(fileName: string, text: string) {
    this.fileName = fileName;
    // Normalize BOM and line endings so downstream stages only deal with "\n".
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
    return { line: lo + 1, column: clamped - starts[lo]! + 1 };
  }

  /** Extract the raw source text covered by a range. */
  slice(range: TextRange): string {
    return this.text.slice(range.start, range.end);
  }
}

/**
 * FNV-1a 64-bit content hash rendered as a hex string. Small, dependency free
 * and stable across platforms which is exactly what the incremental cache
 * needs. Collision resistance is not a security property here.
 */
export function hashText(text: string, seed = 0): string {
  // Two independent 32-bit FNV-1a lanes give a wide, stable fingerprint
  // without relying on BigInt, which the compiler cannot assume at runtime.
  let a = (0xcbf29ce4 ^ seed) >>> 0;
  let b = (0x84222325 ^ seed) >>> 0;
  const prime = 0x01000193;
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    a = Math.imul(a ^ (code & 0xff), prime) >>> 0;
    a = Math.imul(a ^ (code >>> 8), prime) >>> 0;
    b = Math.imul(b ^ (code >>> 8), prime) >>> 0;
    b = Math.imul(b ^ (code & 0xff), prime) >>> 0;
  }
  return (a >>> 0).toString(16).padStart(8, "0") + (b >>> 0).toString(16).padStart(8, "0");
}

/** Combine several hashes/strings into one stable fingerprint. */
export function combineHashes(parts: readonly string[]): string {
  return hashText(parts.join("\u0000"));
}
