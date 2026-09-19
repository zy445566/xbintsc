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
