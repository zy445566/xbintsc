/**
 * Source-level module bundler.
 *
 * xbintsc does not run a linker for modules, so `import`/`export` are lowered
 * at the driver by merging every reachable module into a single source file.
 * Each module's top-level bindings are renamed to a globally unique name and
 * imported names are rewritten to the (renamed) exported binding.
 *
 * This is intentionally simple: it supports named imports/exports, namespace
 * imports, `export default` for declarations, `export ... from` and `export *`.
 * Namespace names are live bindings only at the granularity of the module's
 * final names, and circular graphs are reported as an error.
 */

import { existsSync, readFileSync, statSync } from "node:fs";
import { dirname, isAbsolute, join, resolve } from "node:path";
import {
  ModifierKind,
  SyntaxKind,
  type ExportAssignment,
  type ExportDeclaration,
  type Identifier,
  type ImportDeclaration,
  type Node,
  type ObjectLiteralExpression,
  type PropertyAssignment,
  type SourceFileNode,
  type Statement,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";
import { bind, SymbolKind, type BindResult, type SymbolInfo } from "../binder/binder.js";
import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { Parser } from "../parser/parser.js";

interface ModuleRecord {
  readonly path: string;
  readonly text: string;
  readonly sourceFile: SourceFileNode;
  readonly bind: BindResult;
  readonly prefix: string;
  /** exported name -> final (renamed) local name */
  readonly exports: Map<string, string>;
  /** original local name of every top-level symbol */
  readonly originalNames: Map<number, string>;
  /** final name of every top-level symbol */
  readonly finalNames: Map<number, string>;
}

export interface BundleResult {
  readonly sourceFile: SourceFileNode;
  readonly text: string;
  readonly moduleCount: number;
}

/**
 * Source extensions probed for an extension-less specifier. TypeScript suffixes
 * are tried before their JavaScript counterparts so a project that ships both
 * `foo.ts` and a compiled `foo.js` prefers the source it is compiled from.
 */
const RESOLVE_SUFFIXES = ["", ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"];
/** File extensions a directory `index` entry may use. */
const INDEX_SUFFIXES = [".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"];
/** Conditions consulted, in order, when reading a package `exports` map. */
const EXPORT_CONDITIONS = ["import", "module", "default", "node", "require"];

/** A specifier is a file path (`./x`, `../x`, `/x`) rather than a package. */
function isRelativeSpecifier(specifier: string): boolean {
  return specifier.startsWith(".") || isAbsolute(specifier);
}

/**
 * Resolve a path on disk, tolerating TypeScript's `.js` import convention
 * (`import "./foo.js"` pointing at `foo.ts`) and Node's directory/`index`
 * conventions.
 */
function resolvePath(base: string): string | undefined {
  // A JavaScript specifier may refer to a TypeScript source that will emit it
  // (`import "./foo.js"` pointing at `foo.ts`). Probe the matching TypeScript
  // extensions after the literal path: `.js`/`.jsx` -> `.ts`/`.tsx`,
  // `.mjs` -> `.mts`, `.cjs` -> `.cts`.
  const candidates = [base];
  const jsExtension = /\.((?:m|c)?jsx?)$/.exec(base);
  if (jsExtension) {
    const stem = base.slice(0, jsExtension.index);
    switch (jsExtension[1]) {
      case "jsx":
        candidates.push(`${stem}.tsx`, `${stem}.ts`);
        break;
      case "mjs":
        candidates.push(`${stem}.mts`);
        break;
      case "cjs":
        candidates.push(`${stem}.cts`);
        break;
      default:
        candidates.push(`${stem}.ts`, `${stem}.tsx`);
        break;
    }
  }
  for (const candidate of candidates) {
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  for (const suffix of RESOLVE_SUFFIXES) {
    const candidate = base + suffix;
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  if (existsSync(base) && statSync(base).isDirectory()) return resolveDirectory(base);
  return undefined;
}

/** Resolve a directory to its package entry or an `index` file. */
function resolveDirectory(directory: string): string | undefined {
  const pkg = readPackageJson(directory);
  if (pkg && typeof pkg.main === "string") {
    const main = resolvePath(resolve(directory, pkg.main));
    if (main) return main;
  }
  for (const suffix of INDEX_SUFFIXES) {
    const candidate = join(directory, `index${suffix}`);
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  return undefined;
}

function resolveModule(fromDir: string, specifier: string): string | undefined {
  return resolvePath(resolve(fromDir, specifier));
}

interface PackageJson {
  readonly main?: string;
  readonly module?: string;
  readonly exports?: unknown;
}

function readPackageJson(directory: string): PackageJson | undefined {
  const file = join(directory, "package.json");
  if (!existsSync(file) || !statSync(file).isFile()) return undefined;
  try {
    const parsed = JSON.parse(readFileSync(file, "utf8")) as unknown;
    return parsed && typeof parsed === "object" ? (parsed as PackageJson) : undefined;
  } catch {
    return undefined;
  }
}

/** Split `pkg/sub/path` or `@scope/pkg/sub/path` into package name and subpath. */
function splitPackageSpecifier(specifier: string): { packageName: string; subpath: string } {
  const parts = specifier.split("/");
  if (specifier.startsWith("@")) {
    return { packageName: parts.slice(0, 2).join("/"), subpath: parts.slice(2).join("/") };
  }
  return { packageName: parts[0] ?? specifier, subpath: parts.slice(1).join("/") };
}

/**
 * Resolve a bare specifier against the `node_modules` directories above
 * `fromDir`, following a package's `exports` map and falling back to
 * `module`/`main`/`index`. Packages that only ship CommonJS resolve here too;
 * their `require` calls are then rejected during code generation.
 */
function resolveNodePackage(fromDir: string, specifier: string): string | undefined {
  const { packageName, subpath } = splitPackageSpecifier(specifier);
  if (!packageName || packageName.startsWith(".")) return undefined;
  let directory = fromDir;
  for (;;) {
    const packageDir = join(directory, "node_modules", packageName);
    if (existsSync(packageDir) && statSync(packageDir).isDirectory()) {
      const resolved = subpath
        ? resolvePackageSubpath(packageDir, subpath)
        : resolvePackageEntry(packageDir);
      if (resolved) return resolved;
    }
    const parent = dirname(directory);
    if (parent === directory) return undefined;
    directory = parent;
  }
}

function resolvePackageEntry(packageDir: string): string | undefined {
  const pkg = readPackageJson(packageDir);
  if (pkg) {
    const exported = resolveExportTarget(subpathValue(pkg.exports, "."));
    if (exported) {
      const resolved = resolvePath(resolve(packageDir, exported));
      if (resolved) return resolved;
    }
    if (typeof pkg.module === "string") {
      const resolved = resolvePath(resolve(packageDir, pkg.module));
      if (resolved) return resolved;
    }
    if (typeof pkg.main === "string") {
      const resolved = resolvePath(resolve(packageDir, pkg.main));
      if (resolved) return resolved;
    }
  }
  return resolveDirectory(packageDir);
}

function resolvePackageSubpath(packageDir: string, subpath: string): string | undefined {
  const pkg = readPackageJson(packageDir);
  const exported = resolveExportTarget(subpathValue(pkg?.exports, `./${subpath}`));
  if (exported) {
    const resolved = resolvePath(resolve(packageDir, exported));
    if (resolved) return resolved;
  }
  return resolvePath(join(packageDir, subpath));
}

/** Pick the export target for a subpath (`"."` or `"./sub"`) from an exports map. */
function subpathValue(exportsField: unknown, subpath: string): unknown {
  if (exportsField === undefined) return undefined;
  if (typeof exportsField === "string" || Array.isArray(exportsField)) {
    return subpath === "." ? exportsField : undefined;
  }
  if (!exportsField || typeof exportsField !== "object") return undefined;
  const record = exportsField as Record<string, unknown>;
  const keys = Object.keys(record);
  // A map without `.`-prefixed keys is a condition map for the root export.
  if (!keys.some((key) => key.startsWith("."))) {
    return subpath === "." ? exportsField : undefined;
  }
  if (subpath in record) return record[subpath];
  // Wildcard patterns such as `"./*": "./dist/*.js"`.
  for (const key of keys) {
    const star = key.indexOf("*");
    if (star === -1) continue;
    const prefix = key.slice(0, star);
    const suffix = key.slice(star + 1);
    if (subpath.startsWith(prefix) && subpath.endsWith(suffix)) {
      const matched = subpath.slice(prefix.length, subpath.length - suffix.length);
      return substituteStar(record[key], matched);
    }
  }
  return undefined;
}

function substituteStar(target: unknown, matched: string): unknown {
  if (typeof target === "string") return target.split("*").join(matched);
  if (Array.isArray(target)) return target.map((entry) => substituteStar(entry, matched));
  if (target && typeof target === "object") {
    const record: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(target)) record[key] = substituteStar(value, matched);
    return record;
  }
  return target;
}

/** Pick a runtime string from a (possibly conditional) exports target. */
function resolveExportTarget(target: unknown): string | undefined {
  if (typeof target === "string") return target;
  if (Array.isArray(target)) {
    for (const entry of target) {
      const resolved = resolveExportTarget(entry);
      if (resolved) return resolved;
    }
    return undefined;
  }
  if (target && typeof target === "object") {
    const record = target as Record<string, unknown>;
    for (const condition of EXPORT_CONDITIONS) {
      if (condition in record) {
        const resolved = resolveExportTarget(record[condition]);
        if (resolved) return resolved;
      }
    }
  }
  return undefined;
}

type DependencyResolution =
  | { readonly kind: "external" }
  | { readonly kind: "file"; readonly path: string }
  | { readonly kind: "missing" };

/**
 * Classify an import specifier. Known platform modules (extension modules and
 * `node:` builtins) are left for code generation; relative paths and
 * `node_modules` packages are resolved to a source file to bundle; a relative
 * path that does not exist is an error.
 */
function classifyDependency(
  fromDir: string,
  specifier: string,
  externalSpecifiers: ReadonlySet<string>,
): DependencyResolution {
  if (specifier.startsWith("node:") || externalSpecifiers.has(specifier)) {
    return { kind: "external" };
  }
  const resolved = isRelativeSpecifier(specifier)
    ? resolveModule(fromDir, specifier)
    : resolveNodePackage(fromDir, specifier);
  if (resolved) return { kind: "file", path: resolved };
  // A bare specifier that is not a file may still be a platform module the
  // generator knows about (e.g. when the caller passes no extension registry).
  return isRelativeSpecifier(specifier) ? { kind: "missing" } : { kind: "external" };
}

function hasModifier(node: Node, kind: ModifierKind): boolean {
  const modifiers = (node as { modifiers?: { modifierKind: ModifierKind }[] }).modifiers;
  return !!modifiers && modifiers.some((modifier) => modifier.modifierKind === kind);
}

function declarationName(node: Node): Identifier | undefined {
  switch (node.kind) {
    case SyntaxKind.FunctionDeclaration:
    case SyntaxKind.ClassDeclaration:
    case SyntaxKind.EnumDeclaration:
    case SyntaxKind.InterfaceDeclaration:
    case SyntaxKind.TypeAliasDeclaration:
    case SyntaxKind.ModuleDeclaration:
      return (node as { name?: Identifier }).name;
    case SyntaxKind.VariableDeclaration: {
      const name = (node as VariableDeclaration).name;
      return name.kind === SyntaxKind.Identifier ? name : undefined;
    }
    default:
      return undefined;
  }
}

/** Load, parse and bind the entry module and every module it imports. */
function loadGraph(
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

/** Rewrite a symbol's declaration and every reference to `finalName`. */
function renameSymbol(symbol: SymbolInfo, finalName: string): void {
  for (const declaration of symbol.declarations) {
    const name = declarationName(declaration);
    if (name) (name as { text: string }).text = finalName;
  }
  for (const reference of symbol.references) (reference as { text: string }).text = finalName;
}

/** Rewrite just the references of a symbol (used for imported names). */
function renameReferences(symbol: SymbolInfo, finalName: string): void {
  for (const reference of symbol.references) (reference as { text: string }).text = finalName;
}

function moduleScope(record: ModuleRecord) {
  return record.bind.scopes.get(record.sourceFile);
}

function topLevelSymbols(record: ModuleRecord): SymbolInfo[] {
  const scope = moduleScope(record);
  if (!scope) return [];
  return [...scope.symbols.values()];
}

/**
 * Merge every module's statements into a single file. Dependencies come first
 * (post-order over the import graph), the entry module last.
 */
export function bundleModules(
  entryPath: string,
  diagnostics: DiagnosticBag,
  externalSpecifiers: ReadonlySet<string> = new Set(),
): BundleResult | undefined {
  const records = loadGraph(entryPath, diagnostics, externalSpecifiers);
  if (!records) return undefined;

  const byPath = new Map(records.map((record) => [record.path, record]));
  const dependencyOf = (record: ModuleRecord, specifier: string): ModuleRecord | undefined => {
    const resolution = classifyDependency(dirname(record.path), specifier, externalSpecifiers);
    return resolution.kind === "file" ? byPath.get(resolution.path) : undefined;
  };

  // Phase 1: record every top-level binding and pick a unique final name.
  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      record.originalNames.set(symbol.id, symbol.name);
      if (symbol.kind === SymbolKind.Import) continue;
      record.finalNames.set(symbol.id, record.prefix + symbol.name);
    }
  }

  // Phase 2: collect each module's exported names.
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    const symbolByName = scope.symbols;
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        if (declaration.exportClause && declaration.exportClause.kind === SyntaxKind.NamedExports) {
          for (const specifier of declaration.exportClause.elements) {
            const localName = specifier.propertyName?.text ?? specifier.name.text;
            const exportedName = specifier.name.text;
            if (declaration.moduleSpecifier) {
              const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
              const target = dependency?.exports.get(localName);
              if (target) record.exports.set(exportedName, target);
            } else {
              const symbol = symbolByName.get(localName);
              if (symbol) {
                const finalName = record.finalNames.get(symbol.id) ?? symbol.name;
                record.exports.set(exportedName, finalName);
              }
            }
          }
        } else if (!declaration.exportClause && declaration.moduleSpecifier) {
          // `export * from "..."`: copy every re-exported name.
          const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
          if (dependency) for (const [name, value] of dependency.exports) record.exports.set(name, value);
        }
        continue;
      }
      if (statement.kind === SyntaxKind.ExportAssignment) {
        record.exports.set("default", `${record.prefix}default`);
        continue;
      }
      if (hasModifier(statement, ModifierKind.Default)) {
        const name = declarationName(statement) ?? declarationName((statement as { declaration?: Node }).declaration ?? statement);
        if (name) {
          const symbol = symbolByName.get(name.text);
          if (symbol) record.exports.set("default", record.finalNames.get(symbol.id) ?? symbol.name);
        }
        continue;
      }
      if (hasModifier(statement, ModifierKind.Export)) {
        const names = exportedNames(statement);
        for (const name of names) {
          const symbol = symbolByName.get(name);
          if (symbol) record.exports.set(name, record.finalNames.get(symbol.id) ?? symbol.name);
        }
      }
    }
  }

  // Phase 3: rewrite imported names and record re-exports. Namespace imports
  // are lowered to a synthetic object literal holding every export, built
  // from the dependency's (already renamed) final names.
  const namespaceStatements = new Map<Statement, Statement>();
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    for (const statement of record.sourceFile.statements) {
      if (statement.kind !== SyntaxKind.ImportDeclaration) continue;
      const declaration = statement as ImportDeclaration;
      const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
      const clause = declaration.importClause;
      if (!clause || !dependency) continue;
      if (clause.name) {
        const target = dependency.exports.get("default") ?? dependency.exports.get("default");
        const local = scope.symbols.get(clause.name.text);
        if (target && local) renameReferences(local, target);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          const importedName = specifier.propertyName?.text ?? specifier.name.text;
          const target = dependency.exports.get(importedName);
          const local = scope.symbols.get(specifier.name.text);
          if (target && local) renameReferences(local, target);
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
        const local = scope.symbols.get(bindings.name.text);
        const finalName = `${record.prefix}${bindings.name.text}`;
        if (local) renameReferences(local, finalName);
        namespaceStatements.set(statement, namespaceImportToStatement(statement, finalName, dependency));
      }
    }
  }

  // Phase 4: concatenate statements, dropping imports and pure export wrappers.
  const merged: Statement[] = [];
  const first = records[records.length - 1]!;
  for (const record of records) {
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ImportDeclaration) {
        // External (extension / built-in) imports stay in place for codegen.
        const declaration = statement as ImportDeclaration;
        const resolution = classifyDependency(
          dirname(record.path),
          declaration.moduleSpecifier.value,
          externalSpecifiers,
        );
        if (resolution.kind === "external") {
          merged.push(statement);
        } else {
          const synthetic = namespaceStatements.get(statement);
          if (synthetic) merged.push(synthetic);
        }
        continue;
      }
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        const inner = (declaration as unknown as { declaration?: Statement }).declaration;
        if (inner) merged.push(inner);
        continue;
      }
      if (statement.kind === SyntaxKind.ExportAssignment) {
        const assignment = statement as ExportAssignment;
        merged.push(exportAssignmentToStatement(record, assignment));
        continue;
      }
      merged.push(statement);
    }
  }

  // Phase 5: rename every top-level binding (declaration + references) to its
  // unique final name, now that import references have been rewritten.
  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      if (symbol.kind === SymbolKind.Import) continue;
      const finalName = record.finalNames.get(symbol.id);
      if (finalName) renameSymbol(symbol, finalName);
    }
  }

  const sourceFile: SourceFileNode = {
    kind: SyntaxKind.SourceFile,
    statements: merged,
    fileName: first.path,
    text: records.map((record) => record.text).join("\n"),
    start: 0,
    end: 0,
  };
  return { sourceFile, text: sourceFile.text, moduleCount: records.length };
}

function exportedNames(statement: Statement): string[] {
  const names: string[] = [];
  if (statement.kind === SyntaxKind.VariableStatement) {
    for (const declaration of (statement as VariableStatement).declarationList.declarations) {
      collectBindingIdentifiers(declaration.name, names);
    }
  } else {
    const name = declarationName(statement);
    if (name) names.push(name.text);
  }
  return names;
}

function collectBindingIdentifiers(name: import("../ast/declarations.js").BindingName, out: string[]): void {
  if (name.kind === SyntaxKind.Identifier) {
    out.push(name.text);
    return;
  }
  const elements = name.kind === SyntaxKind.ArrayBindingPattern ? name.elements : name.elements;
  for (const element of elements) {
    if (element) collectBindingIdentifiers(element.name, out);
  }
}

/** Lower `import * as ns from "./mod"` to `const m0_ns = { ...exports }`. */
function namespaceImportToStatement(
  statement: Statement,
  finalName: string,
  dependency: ModuleRecord,
): Statement {
  const properties: PropertyAssignment[] = [];
  for (const [exportedName, target] of dependency.exports) {
    properties.push({
      kind: SyntaxKind.PropertyAssignment,
      name: { kind: SyntaxKind.Identifier, text: exportedName, start: statement.start, end: statement.start },
      initializer: { kind: SyntaxKind.Identifier, text: target, start: statement.start, end: statement.start },
      start: statement.start,
      end: statement.start,
    });
  }
  const initializer: ObjectLiteralExpression = {
    kind: SyntaxKind.ObjectLiteralExpression,
    properties,
    start: statement.start,
    end: statement.end,
  };
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name: { kind: SyntaxKind.Identifier, text: finalName, start: statement.start, end: statement.start },
    exclamation: false,
    initializer,
    start: statement.start,
    end: statement.end,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarationKind: "const" as const,
    declarations: [declaration],
    start: statement.start,
    end: statement.end,
  };
  return {
    kind: SyntaxKind.VariableStatement,
    declarationList: list,
    modifiers: [],
    start: statement.start,
    end: statement.end,
  };
}

/** Turn `export default expr` into a synthetic `const` declaration. */
function exportAssignmentToStatement(record: ModuleRecord, assignment: ExportAssignment): Statement {
  const name: Identifier = {
    kind: SyntaxKind.Identifier,
    text: `${record.prefix}default`,
    start: assignment.start,
    end: assignment.start,
  };
  record.exports.set("default", name.text);
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name,
    exclamation: false,
    initializer: assignment.expression,
    start: assignment.start,
    end: assignment.end,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarationKind: "const" as const,
    declarations: [declaration],
    start: assignment.start,
    end: assignment.end,
  };
  const statement: VariableStatement = {
    kind: SyntaxKind.VariableStatement,
    declarationList: list,
    modifiers: [],
    start: assignment.start,
    end: assignment.end,
  };
  return statement;
}
