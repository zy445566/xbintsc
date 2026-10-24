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

import type { SourceFileNode } from "../ast/nodes.js";
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
): void {
  for (const identifier of binding.unresolved) {
    if (isDependencyNode(sourceFile, identifier)) continue;
    const name = identifier.text;
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
