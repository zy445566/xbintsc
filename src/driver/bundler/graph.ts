/**
 * Import-graph loading: parse and bind the entry module and every module it
 * reaches, in dependency order.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname } from "node:path";
import { SyntaxKind, type ExportDeclaration, type ImportDeclaration, type Statement } from "../../ast/nodes.js";
import { bind } from "../../binder/binder.js";
import { DiagnosticBag, DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { SourceFile } from "../../diagnostics/source.js";
import { Parser } from "../../parser/parser.js";
import { classifyDependency } from "./resolve.js";
import type { ModuleRecord } from "./types.js";

function moduleSpecifierOf(statement: Statement): string | undefined {
  if (statement.kind === SyntaxKind.ImportDeclaration) {
    return (statement as ImportDeclaration).moduleSpecifier.value;
  }
  if (statement.kind === SyntaxKind.ExportDeclaration) {
    const specifier = (statement as ExportDeclaration).moduleSpecifier;
    if (specifier) return specifier.value;
  }
  return undefined;
}

/** Load, parse and bind the entry module and every module it imports. */
export function loadGraph(
  entryPath: string,
  diagnostics: DiagnosticBag,
  externalSpecifiers: ReadonlySet<string>,
): ModuleRecord[] | undefined {
  const records = new Map<string, ModuleRecord>();
  const order: ModuleRecord[] = [];
  const visiting = new Set<string>();
  let counter = 0;
  let failed = false;

  const load = (path: string): ModuleRecord | undefined => {
    const existing = records.get(path);
    if (existing) return existing;
    if (visiting.has(path)) {
      diagnostics.error(DiagnosticCode.CodegenError, `Circular import detected involving '${path}'`);
      failed = true;
      return undefined;
    }
    if (!existsSync(path)) {
      diagnostics.error(DiagnosticCode.CodegenError, `Cannot find module '${path}'`);
      failed = true;
      return undefined;
    }
    visiting.add(path);
    const text = readFileSync(path, "utf8");
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
    };
    records.set(path, record);

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
      load(dependency.path);
    }
    visiting.delete(path);
    order.push(record);
    return record;
  };

  load(entryPath);
  if (failed) return undefined;
  return order;
}
