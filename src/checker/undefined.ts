/**
 * Unresolved names: an identifier that is neither a declaration in any
 * enclosing scope nor a runtime global.
 *
 * The binder already collects exactly this set (`BindResult.unresolved`); the
 * checker only decides which entries are legitimate globals and reports the
 * rest. Keeping the decision here (instead of in code generation) means a name
 * error no longer depends on the identifier being lowered — dead code and
 * unreachable branches are checked too.
 */

import type { Identifier, SourceFileNode } from "../ast/nodes.js";
import type { BindResult } from "../binder/binder.js";
import { DiagnosticCode, type DiagnosticBag } from "../diagnostics/diagnostic.js";
import { isKnownGlobalName } from "./globals.js";
import { fileNameOf, isDependencyNode } from "./source.js";

/**
 * Names that are unresolved by design: `import.meta` is lowered through a
 * namespace getter, `require` gets a dedicated "use ESM import" diagnostic
 * from code generation, and `<error>` is the parser's recovery node.
 */
const SPECIAL_NAMES: ReadonlySet<string> = new Set(["import.meta", "require", "<error>"]);

export function checkUnresolvedNames(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
  extensionBuiltins?: ReadonlySet<string>,
  globalHints: Readonly<Record<string, string>> = {},
): void {
  for (const identifier of binding.unresolved) {
    /* Dependencies are not the user's code: they may contain dead browser
     * branches (`highlight.js` references `window` under a runtime guard), so
     * the same exemption that hides other unresolved dependency names applies
     * to extension globals. */
    if (isDependencyNode(sourceFile, identifier)) continue;
    const name = identifier.text;
    /* A global whose runtime lives in an extension that is known but not
     * enabled: report the flag instead of treating it as a known global (it
     * would otherwise read `undefined` or fail later with an unresolved
     * `xt_*` symbol at link time). */
    const extension = Object.hasOwn(globalHints, name) ? globalHints[name] : undefined;
    if (extension !== undefined) {
      reportMissingGlobal(sourceFile, identifier, extension, diagnostics);
      continue;
    }
    if (SPECIAL_NAMES.has(name)) continue;
    if (isKnownGlobalName(name, extensionBuiltins)) continue;
    diagnostics.error(
      DiagnosticCode.CannotFindName,
      `Cannot find name '${name}'`,
      identifier,
      fileNameOf(sourceFile, identifier),
    );
  }
}

function reportMissingGlobal(
  sourceFile: SourceFileNode,
  identifier: Identifier,
  extension: string,
  diagnostics: DiagnosticBag,
): void {
  diagnostics.error(
    DiagnosticCode.ModuleNotFound,
    `global '${identifier.text}' is provided by the '${extension}' extension; pass --ext ${extension}`,
    identifier,
    fileNameOf(sourceFile, identifier),
  );
}

/**
 * Same check without the full semantic checker (`--no-check`). Code
 * generation would otherwise emit `undefined` for `const p = process` or an
 * unresolved `xt_process_get` reference for `process.env`.
 */
export function reportMissingExtensionGlobals(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
  globalHints: Readonly<Record<string, string>> = {},
): void {
  for (const identifier of binding.unresolved) {
    if (isDependencyNode(sourceFile, identifier)) continue;
    const extension = Object.hasOwn(globalHints, identifier.text)
      ? globalHints[identifier.text]
      : undefined;
    if (extension !== undefined) reportMissingGlobal(sourceFile, identifier, extension, diagnostics);
  }
}
