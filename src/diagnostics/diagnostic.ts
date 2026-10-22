import type { SourceFile } from "./source.js";

export type DiagnosticCategory = "error" | "warning" | "info";

/** Stable machine readable codes, grouped by pipeline stage. */
export enum DiagnosticCode {
  // Lexer 1xxx
  UnterminatedString = 1001,
  UnterminatedTemplate = 1002,
  UnterminatedComment = 1003,
  InvalidCharacter = 1004,
  InvalidNumber = 1005,
  InvalidEscape = 1006,

  // Parser 2xxx
  UnexpectedToken = 2001,
  ExpectedToken = 2002,
  ExpectedIdentifier = 2003,
  UnexpectedEof = 2004,
  InvalidAssignmentTarget = 2005,
  DuplicateDefault = 2006,
  InvalidTypeSyntax = 2007,
  NestingTooDeep = 2008,

  // Binder 3xxx
  DuplicateIdentifier = 3001,
  CannotFindName = 3002,
  IllegalRedeclaration = 3003,
  CannotAssignToConst = 3004,

  // Checker 4xxx
  TypeMismatch = 4001,
  NotCallable = 4002,
  PropertyNotFound = 4003,
  ArgumentCountMismatch = 4004,
  UnsupportedFeature = 4005,

  // Codegen 5xxx
  CodegenError = 5001,

  // Driver 6xxx
  ModuleNotFound = 6001,
  IOError = 6002,
  ToolchainError = 6003,
  IncrementalCacheError = 6004,
  CircularDependency = 6005,
}

export interface Diagnostic {
  readonly category: DiagnosticCategory;
  readonly code: DiagnosticCode;
  readonly message: string;
  readonly fileName?: string;
  readonly start?: number;
  readonly end?: number;
}

export class DiagnosticError extends Error {
  readonly diagnostics: readonly Diagnostic[];
  constructor(diagnostics: Diagnostic | readonly Diagnostic[]) {
    const list = Array.isArray(diagnostics) ? diagnostics : [diagnostics];
    super(list.map((d) => formatDiagnostic(d)).join("\n"));
    this.name = "DiagnosticError";
    this.diagnostics = list;
  }
}

export class DiagnosticBag {
  private readonly items: Diagnostic[] = [];

  get diagnostics(): readonly Diagnostic[] {
    return this.items;
  }

  get hasErrors(): boolean {
    return this.items.some((d) => d.category === "error");
  }

  /** Snapshot the current size so a speculative parse can roll back. */
  mark(): number {
    return this.items.length;
  }

  /** Discard every diagnostic added after `mark()` was taken. */
  reset(mark: number): void {
    this.items.length = mark;
  }

  error(code: DiagnosticCode, message: string, range?: { start: number; end: number }, fileName?: string): void {
    this.items.push({ category: "error", code, message, fileName, start: range?.start, end: range?.end });
  }

  warning(code: DiagnosticCode, message: string, range?: { start: number; end: number }, fileName?: string): void {
    this.items.push({ category: "warning", code, message, fileName, start: range?.start, end: range?.end });
  }

  add(diagnostic: Diagnostic): void {
    this.items.push(diagnostic);
  }

  addAll(diagnostics: readonly Diagnostic[]): void {
    this.items.push(...diagnostics);
  }

  throwIfErrors(): void {
    if (this.hasErrors) throw new DiagnosticError(this.items);
  }
}

/**
 * Render a diagnostic with a source excerpt and a caret underline. Falls back
 * to a plain "file:line:col" form when no source file is available.
 */
export function formatDiagnostic(d: Diagnostic, source?: SourceFile): string {
  const severity = d.category;
  let location = d.fileName ?? "<unknown>";
  let excerpt = "";
  if (source && d.start !== undefined) {
    const { line, column } = source.positionAt(d.start);
    location = `${source.fileName}:${line}:${column}`;
    const lineText = source.text.split("\n")[line - 1] ?? "";
    const end = d.end ?? d.start;
    const endCol = source.positionAt(end).column;

    /* Long lines (a minified bundle, a generated one-liner, deeply nested
     * source) would otherwise print tens of thousands of characters and make
     * the actual message impossible to find. Keep a window around the caret. */
    const maxExcerpt = 160;
    const caretIndex = Math.max(0, column - 1);
    let windowStart = 0;
    let windowEnd = lineText.length;
    if (lineText.length > maxExcerpt) {
      windowStart = Math.max(0, caretIndex - Math.floor(maxExcerpt / 2));
      windowEnd = Math.min(lineText.length, windowStart + maxExcerpt);
      if (windowEnd === lineText.length) windowStart = Math.max(0, windowEnd - maxExcerpt);
    }
    const prefix = windowStart > 0 ? "\u2026" : "";
    const suffix = windowEnd < lineText.length ? "\u2026" : "";
    const visible = lineText.slice(windowStart, windowEnd);
    const visibleCaret = caretIndex - windowStart;
    const caretWidth = Math.max(1, Math.min(endCol - column, visible.length - visibleCaret));
    const caretPad = " ".repeat(prefix.length + Math.max(0, visibleCaret));
    const caret = "^".repeat(caretWidth);
    excerpt = `\n${prefix}${visible}${suffix}\n${caretPad}${caret}`;
  }
  return `${location} - ${severity} TS${d.code}: ${d.message}${excerpt}`;
}

export function formatDiagnostics(diagnostics: readonly Diagnostic[], sources?: Map<string, SourceFile>): string {
  return diagnostics
    .map((d) => formatDiagnostic(d, d.fileName ? sources?.get(d.fileName) : undefined))
    .join("\n");
}
