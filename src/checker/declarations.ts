/**
 * Redeclarations: the binder records same-scope declarations that cannot merge
 * (`let x` twice, `function f` followed by `const f`, ...). This pass turns
 * those facts into diagnostics.
 */

import type { SourceFileNode } from "../ast/nodes.js";
import type { BindResult } from "../binder/binder.js";
import { DiagnosticCode, type DiagnosticBag } from "../diagnostics/diagnostic.js";
import { fileNameOf, isDependencyNode } from "./source.js";

export function reportDeclarationConflicts(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
): void {
  for (const conflict of binding.conflicts) {
    if (isDependencyNode(sourceFile, conflict.node)) continue;
    diagnostics.error(
      DiagnosticCode.DuplicateIdentifier,
      `Duplicate identifier '${conflict.name}'.`,
      conflict.node,
      fileNameOf(sourceFile, conflict.node),
    );
  }
}
