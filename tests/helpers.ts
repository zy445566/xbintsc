/** Shared helpers for the per-module test suites. */

import { DiagnosticBag, type Diagnostic } from "../src/diagnostics/diagnostic.js";
import { SourceFile } from "../src/diagnostics/source.js";
import { Scanner } from "../src/lexer/scanner.js";
import { Parser } from "../src/parser/parser.js";
import { bind, type BindResult } from "../src/binder/binder.js";
import { generate } from "../src/codegen/llvm.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../src/extensions/registry.js";
import { realRunner } from "../src/driver/toolchain.js";
import type { Token } from "../src/lexer/token.js";

export function lex(text: string): { tokens: Token[]; diagnostics: readonly Diagnostic[] } {
  const source = new SourceFile("test.ts", text);
  const diagnostics = new DiagnosticBag();
  const scanner = new Scanner(source, diagnostics);
  return { tokens: scanner.tokenize(), diagnostics: diagnostics.diagnostics };
}

export function parse(text: string) {
  const source = new SourceFile("test.ts", text);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(source, diagnostics);
  const file = parser.parseSourceFile();
  return { file, diagnostics: diagnostics.diagnostics };
}

export function bindSource(text: string): { result: BindResult; diagnostics: readonly Diagnostic[] } {
  const { file, diagnostics } = parse(text);
  const bag = new DiagnosticBag();
  bag.addAll(diagnostics);
  const result = bind(file);
  return { result, diagnostics: bag.diagnostics };
}

export function compileToIr(text: string, extensions?: ExtensionRegistry) {
  const { file, diagnostics: parseDiagnostics } = parse(text);
  const diagnostics = new DiagnosticBag();
  diagnostics.addAll(parseDiagnostics);
  const registry = extensions ?? createDefaultRegistry();
  const { ir, binding } = generate(file, diagnostics, { builtins: registry.builtins() });
  return { ir, binding, diagnostics: diagnostics.diagnostics };
}

export function hasClang(): boolean {
  return realRunner.run(process.env.XTSC_CLANG ?? "clang", ["--version"]).status === 0;
}
