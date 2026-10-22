/**
 * Import-graph loading: parse and bind the entry module and every module it
 * reaches, in dependency order.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, extname, relative } from "node:path";
import { SyntaxKind, type ExportDeclaration, type ImportDeclaration, type NamedExports, type Statement } from "../../ast/nodes.js";
import { bind } from "../../binder/binder.js";
import { DiagnosticBag, DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { SourceFile } from "../../diagnostics/source.js";
import { sourceTextBytes } from "../../diagnostics/source-text.js";
import { Parser } from "../../parser/parser.js";
import type { AssetLoader } from "../../extensions/registry.js";
import { classifyDependency } from "./resolve.js";
import { collectRequireSites, isNodeModulesPath, usesCommonJS } from "./commonjs.js";
import type { ModuleRecord } from "./types.js";

/**
 * True when an import has no runtime binding, so it is not a module edge:
 * `import type { T } from "..."` or `import { type T } from "..."`. A
 * side-effect-only import (`import "..."`) and an empty clause
 * (`import {} from "..."`) both evaluate the module and *are* edges. Types are
 * erased, so a type-only import must not pull the target into the bundle —
 * and, in particular, must not turn the mutual `import type` references
 * between source files into a circular-import error.
 */
function isTypeOnlyImport(declaration: ImportDeclaration): boolean {
  const clause = declaration.importClause;
  if (!clause) return false;
  if (clause.isTypeOnly) return true;
  if (clause.name) return false;
  const bindings = clause.namedBindings;
  if (!bindings) return true;
  if (bindings.kind === SyntaxKind.NamespaceImport) return false;
  // `import {} from "..."` is an empty clause but still evaluates the module.
  return bindings.elements.length > 0 && bindings.elements.every((element) => element.isTypeOnly);
}

/** True when an export declaration only re-exports types. */
function isTypeOnlyExport(declaration: ExportDeclaration): boolean {
  if (declaration.isTypeOnly) return true;
  const clause = declaration.exportClause;
  if (!clause || clause.kind !== SyntaxKind.NamedExports) return false;
  const elements = (clause as NamedExports).elements;
  // `export {} from "..."` is a side-effect re-export, not a type-only one.
  return elements.length > 0 && elements.every((element) => element.isTypeOnly);
}

function moduleSpecifierOf(statement: Statement): string | undefined {
  if (statement.kind === SyntaxKind.ImportDeclaration) {
    const declaration = statement as ImportDeclaration;
    if (isTypeOnlyImport(declaration)) return undefined;
    return declaration.moduleSpecifier.value;
  }
  if (statement.kind === SyntaxKind.ExportDeclaration) {
    const declaration = statement as ExportDeclaration;
    if (isTypeOnlyExport(declaration)) return undefined;
    const specifier = declaration.moduleSpecifier;
    if (specifier) return specifier.value;
  }
  return undefined;
}

/** Load, parse and bind the entry module and every module it imports. */
export function loadGraph(
  entryPath: string,
  diagnostics: DiagnosticBag,
  externalSpecifiers: ReadonlySet<string>,
  assetLoaders: Readonly<Record<string, AssetLoader>> = {},
): ModuleRecord[] | undefined {
  const records = new Map<string, ModuleRecord>();
  const order: ModuleRecord[] = [];
  /* Modules on the current depth-first path, for cycle reporting. A module is
   * added to `records` only after its dependencies finish, so a back edge is
   * still "visiting" when it is reached. */
  const stack: string[] = [];
  let counter = 0;
  let failed = false;

  const load = (
    path: string,
    origin?: { readonly file: string; readonly statement: Statement },
  ): ModuleRecord | undefined => {
    const existing = records.get(path);
    if (existing) return existing;
    if (stack.includes(path)) {
      const start = stack.indexOf(path);
      const cycle = [...stack.slice(start), path]
        .map((entry) => relative(dirname(entryPath), entry))
        .join(" -> ");
      diagnostics.error(
        DiagnosticCode.CircularDependency,
        `Circular import detected: ${cycle}`,
        origin?.statement,
        origin?.file,
      );
      failed = true;
      return undefined;
    }
    if (!existsSync(path)) {
      diagnostics.error(DiagnosticCode.CodegenError, `Cannot find module '${path}'`);
      failed = true;
      return undefined;
    }
    stack.push(path);
    const raw = sourceTextBytes(readFileSync(path, "utf8"));
    /* An extension may rewrite an imported asset (e.g. `.html`) into a TS
     * module before it is parsed. The core only sees the transformed text. */
    const loader = assetLoaders[extname(path).toLowerCase()];
    let text = raw;
    if (loader) {
      try {
        text = loader(path, raw).moduleSource;
      } catch (error) {
        diagnostics.error(
          DiagnosticCode.CodegenError,
          error instanceof Error ? error.message : String(error),
        );
        failed = true;
        stack.pop();
        return undefined;
      }
    }
    const file = new SourceFile(path, text);
    const parser = new Parser(file, diagnostics);
    const sourceFile = parser.parseSourceFile();
    const bindResult = bind(sourceFile);
    const record: ModuleRecord = {
      path,
      text,
      sourceFile,
      bind: bindResult,
      prefix: `m${counter++}_`,
      exports: new Map(),
      originalNames: new Map(),
      finalNames: new Map(),
      commonjs: false,
    };
    // `require` / `module` / `exports` are only honoured inside `node_modules`,
    // so user code keeps its ESM-only diagnostic.
    record.commonjs = isNodeModulesPath(path) && usesCommonJS(bindResult);

    for (const statement of sourceFile.statements) {
      const specifier = moduleSpecifierOf(statement);
      if (!specifier) continue;
      const dependency = classifyDependency(dirname(path), specifier, externalSpecifiers);
      if (dependency.kind === "external") continue;
      if (dependency.kind === "missing") {
        diagnostics.error(
          DiagnosticCode.CodegenError,
          `Cannot resolve module '${specifier}' from '${path}'`,
          statement,
          path,
        );
        failed = true;
        continue;
      }
      load(dependency.path, { file: path, statement });
    }

    // A CommonJS module's `require("...")` calls are dynamic import edges: the
    // target must be bundled (and evaluated) before this module runs.
    if (record.commonjs) {
      for (const site of collectRequireSites(sourceFile, bindResult)) {
        const dependency = classifyDependency(dirname(path), site.specifier, externalSpecifiers);
        if (dependency.kind === "file") load(dependency.path);
      }
    }
    stack.pop();
    records.set(path, record);
    order.push(record);
    return record;
  };

  load(entryPath);
  if (failed) return undefined;
  return order;
}
