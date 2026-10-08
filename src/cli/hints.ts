/**
 * Documentation pointers for diagnostics.
 *
 * Every failure printed by the CLI ends with a `hint:` line naming the document
 * that explains what to do next. The messages already say what is wrong; these
 * lines say where to read more, which is what keeps an agent (or a person) from
 * re-deriving the language subset one failed build at a time.
 *
 * The paths are relative to the repository root, so they stay clickable in
 * GitHub's rendered output and on a terminal that links paths.
 */

import { DiagnosticCode, type Diagnostic } from "../diagnostics/diagnostic.js";

/** Where a reader should go next. */
export interface DocumentationPointer {
  /** Message printed after `hint:`. */
  readonly text: string;
  /** Repository-relative path to the document. */
  readonly path: string;
}

export const DOC_LANGUAGE_SUPPORT = "doc/ai/language-support.md";
export const DOC_EXTENSIONS = "doc/ai/extensions.md";
export const DOC_TROUBLESHOOTING = "doc/ai/troubleshooting.md";
export const DOC_CLI = "doc/ai/cli.md";
export const DOC_REQUIREMENTS = "doc/requirements.md";

const CHECKER_HINT: DocumentationPointer = {
  text: "the compiler supports a subset of TypeScript; check what is implemented before rewriting",
  path: DOC_LANGUAGE_SUPPORT,
};

const REQUIREMENT_HINT: DocumentationPointer = {
  text: "xbintsc needs clang 16 or newer; run `xbintsc doctor` to see the resolved toolchain",
  path: DOC_REQUIREMENTS,
};

/** Hint for a clang failure: a missing or too-old clang has a specific answer. */
export function toolchainPointer(message: string): DocumentationPointer {
  const usesTypedPointers = /defined with type '\[.*i8\]\*' but expected 'i8\*'/.test(message);
  if (usesTypedPointers || /No C compiler found/i.test(message)) return REQUIREMENT_HINT;
  return {
    text: "read the clang output above, then the toolchain section of the troubleshooting guide",
    path: DOC_TROUBLESHOOTING,
  };
}

/** A thrown failure the CLI can name, in table order. */
export interface ThrownFailure {
  /** Tested against the thrown message. */
  readonly match: RegExp;
  readonly code: DiagnosticCode;
  readonly pointer: DocumentationPointer;
}

/**
 * Failures the CLI reports as a message instead of a diagnostic. Matching on the
 * text is what lets each one reach the document that actually explains it —
 * a missing manifest and an unknown extension are both "cannot read something",
 * but only one of them is answered by the CLI reference.
 */
const THROWN_FAILURES: readonly ThrownFailure[] = [
  {
    match: /native extension manifest/i,
    code: DiagnosticCode.IOError,
    pointer: {
      text: "check the manifest path and its JSON; objects are relative to the manifest file",
      path: DOC_EXTENSIONS,
    },
  },
  {
    match: /Unknown extension/i,
    code: DiagnosticCode.IOError,
    pointer: {
      text: "run `xbintsc help` for the flag, and see the built-in extensions this build ships",
      path: DOC_EXTENSIONS,
    },
  },
  {
    match: /config|JSON/i,
    code: DiagnosticCode.IOError,
    pointer: {
      text: "check the project config path and its fields",
      path: DOC_CLI,
    },
  },
];

/** The failure a thrown error's message describes, if any. */
export function thrownFailure(message: string): ThrownFailure | undefined {
  return THROWN_FAILURES.find((failure) => failure.match.test(message));
}

/**
 * The completion for an unclassified thrown error: the toolchain's own wording
 * is the only thing worth directing a reader to.
 */
export const UNEXPECTED_FAILURE_HINT: DocumentationPointer = {
  text: "this failure has no dedicated guide; check the resolved toolchain and the source paths",
  path: DOC_TROUBLESHOOTING,
};

/**
 * The document that explains a diagnostic, or `undefined` for internal errors
 * that no reader can act on.
 */
export function documentationFor(diagnostic: Diagnostic): DocumentationPointer | undefined {
  switch (diagnostic.code) {
    // Lexer, parser and binder report a precise location; the guidance is the
    // same subset reference.
    case DiagnosticCode.UnterminatedString:
    case DiagnosticCode.UnterminatedTemplate:
    case DiagnosticCode.UnterminatedComment:
    case DiagnosticCode.InvalidCharacter:
    case DiagnosticCode.InvalidNumber:
    case DiagnosticCode.InvalidEscape:
    case DiagnosticCode.UnexpectedToken:
    case DiagnosticCode.ExpectedToken:
    case DiagnosticCode.ExpectedIdentifier:
    case DiagnosticCode.UnexpectedEof:
    case DiagnosticCode.InvalidAssignmentTarget:
    case DiagnosticCode.DuplicateDefault:
    case DiagnosticCode.InvalidTypeSyntax:
    case DiagnosticCode.DuplicateIdentifier:
    case DiagnosticCode.CannotFindName:
    case DiagnosticCode.IllegalRedeclaration:
    case DiagnosticCode.CannotAssignToConst:
    case DiagnosticCode.UnsupportedFeature:
      return CHECKER_HINT;

    // Every codegen and bundler error carries its own message; the subset page
    // is what separates "you wrote unsupported code" from "you wrote a bug".
    case DiagnosticCode.CodegenError:
      return CHECKER_HINT;

    case DiagnosticCode.ModuleNotFound:
      return {
        text: "Node modules need the `node` extension (`--ext node`); third-party npm packages are not supported",
        path: DOC_EXTENSIONS,
      };

    case DiagnosticCode.IOError:
      return {
        text: "check the path and the project config; paths in xbintsc.config.json resolve against that file",
        path: DOC_TROUBLESHOOTING,
      };

    case DiagnosticCode.ToolchainError:
      return toolchainPointer(diagnostic.message);

    case DiagnosticCode.IncrementalCacheError:
      return {
        text: "the incremental cache could not be trusted; retry with `--force` or remove the cache directory",
        path: DOC_CLI,
      };

    // The checker codes are defined but never emitted today.
    case DiagnosticCode.TypeMismatch:
    case DiagnosticCode.NotCallable:
    case DiagnosticCode.PropertyNotFound:
    case DiagnosticCode.ArgumentCountMismatch:
      return CHECKER_HINT;
  }
}

/** Render one `hint:` line, or `undefined` when the code has no document. */
export function formatDocumentationHint(diagnostic: Diagnostic): string | undefined {
  const pointer = documentationFor(diagnostic);
  if (!pointer) return undefined;
  return `hint: ${pointer.text} — see ${pointer.path}`;
}

/**
 * One hint line per failing diagnostic, in first-seen order and without
 * repeating a document the reader has already been sent to.
 */
export function documentationHints(diagnostics: readonly Diagnostic[]): string[] {
  const lines: string[] = [];
  const seen = new Set<string>();
  for (const diagnostic of diagnostics) {
    if (diagnostic.category !== "error") continue;
    const line = formatDocumentationHint(diagnostic);
    if (line === undefined || seen.has(line)) continue;
    seen.add(line);
    lines.push(line);
  }
  return lines;
}

/** Render one hint line for an already-classified thrown failure. */
export function formatThrownHint(pointer: DocumentationPointer): string {
  return `hint: ${pointer.text} — see ${pointer.path}`;
}
