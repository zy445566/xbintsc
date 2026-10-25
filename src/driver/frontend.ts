/**
 * Front end: source text in, LLVM IR out (no toolchain involved).
 *
 * `compileString` compiles one in-memory source text; `compileEntry` reads a
 * file and first bundles every reachable module. `build` reuses the same
 * `parseEntry` step so the module graph is resolved identically for `emit`,
 * `run` and `build`.
 */

import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { DiagnosticBag } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { sourceTextBytes } from "../diagnostics/source-text.js";
import { Parser } from "../parser/parser.js";
import { generate } from "../codegen/llvm.js";
import { SyntaxKind, type SourceFileNode } from "../ast/nodes.js";
import { bundleModules } from "./modules.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";
import type { CompileStringResult } from "./options.js";

/**
 * Every module specifier the registry can resolve at code generation time (the
 * registered platform modules plus the hints for known-but-disabled
 * extensions). Bare imports outside this set are looked up in `node_modules`
 * and bundled as source.
 */
function externalModuleSpecifiers(registry: ExtensionRegistry): Set<string> {
  return new Set([
    ...Object.keys(registry.modules()),
    ...Object.keys(registry.moduleHints()),
  ]);
}

/** Front-end switches shared by `compileString` and `compileEntry`. */
export interface FrontendOptions {
  readonly debug?: boolean;
  /** Run the semantic checker (default true; `--no-check` disables it). */
  readonly check?: boolean;
}

/** A parsed (and, for module entries, already bundled) entry file. */
export interface ParsedEntry {
  readonly absoluteEntry: string;
  readonly sourceText: string;
  readonly sourceFile: SourceFileNode;
  /** The text the incremental cache hashes (the bundled text for modules). */
  readonly cacheText: string;
}

/**
 * Parse an entry file, bundling its module graph first when it uses
 * `import`/`export`. The caller owns `diagnostics` and must not run code
 * generation when it has errors (the tree would be missing nodes).
 */
export function parseEntry(
  entryPath: string,
  registry: ExtensionRegistry,
  diagnostics: DiagnosticBag,
): ParsedEntry {
  const absoluteEntry = resolve(entryPath);
  const sourceText = sourceTextBytes(readFileSync(absoluteEntry, "utf8"));
  const file = new SourceFile(absoluteEntry, sourceText);
  const parser = new Parser(file, diagnostics);
  let sourceFile = parser.parseSourceFile();
  const isModule = sourceFile.statements.some(
    (statement) =>
      statement.kind === SyntaxKind.ImportDeclaration ||
      statement.kind === SyntaxKind.ExportDeclaration ||
      statement.kind === SyntaxKind.ExportAssignment,
  );
  let cacheText = sourceText;
  if (isModule) {
    const bundled = bundleModules(absoluteEntry, diagnostics, externalModuleSpecifiers(registry), registry.assetLoaders());
    if (bundled) {
      sourceFile = bundled.sourceFile;
      cacheText = bundled.text;
    }
  }
  return { absoluteEntry, sourceText, sourceFile, cacheText };
}

/** Compile TypeScript source text to LLVM IR without touching the toolchain. */
export function compileString(
  source: string,
  fileName = "input.ts",
  extensions?: ExtensionRegistry,
  options: FrontendOptions = {},
): CompileStringResult {
  const file = new SourceFile(fileName, source);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(file, diagnostics);
  const sourceFile = parser.parseSourceFile();
  const registry = extensions ?? createDefaultRegistry();
  /* Code generation assumes a well-formed tree (binder and codegen both read
   * required children), so never lower a file that already failed to parse. */
  if (diagnostics.hasErrors) return { ir: "", diagnostics: diagnostics.diagnostics };
  const { ir } = generate(sourceFile, diagnostics, {
    builtins: registry.builtins(),
    globalValues: registry.globalValues(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
    debug: options.debug === true,
    check: options.check !== false,
  });
  return { ir, diagnostics: diagnostics.diagnostics };
}

/**
 * Compile a file to LLVM IR, bundling every reachable relative module first so
 * that imports from other TypeScript sources resolve exactly like `build`.
 */
export function compileEntry(
  entryPath: string,
  extensions?: ExtensionRegistry,
  options: FrontendOptions = {},
): CompileStringResult {
  const registry = extensions ?? createDefaultRegistry();
  const diagnostics = new DiagnosticBag();
  const parsed = parseEntry(entryPath, registry, diagnostics);
  if (diagnostics.hasErrors) return { ir: "", diagnostics: diagnostics.diagnostics };
  const { ir } = generate(parsed.sourceFile, diagnostics, {
    builtins: registry.builtins(),
    globalValues: registry.globalValues(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
    debug: options.debug === true,
    check: options.check !== false,
  });
  return { ir, diagnostics: diagnostics.diagnostics };
}
