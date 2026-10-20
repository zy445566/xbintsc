/** Shared helpers for the per-module test suites. */

import { DiagnosticBag, type Diagnostic } from "../src/diagnostics/diagnostic.js";
import { SourceFile } from "../src/diagnostics/source.js";
import { sourceTextBytes } from "../src/diagnostics/source-text.js";
import { Scanner } from "../src/lexer/scanner.js";
import { Parser } from "../src/parser/parser.js";
import { bind, type BindResult } from "../src/binder/binder.js";
import { generate } from "../src/codegen/llvm.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../src/extensions/registry.js";
import { MINIMUM_CLANG_MAJOR } from "../src/driver/toolchain.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";
import type { Token } from "../src/lexer/token.js";

export function lex(text: string): { tokens: Token[]; diagnostics: readonly Diagnostic[] } {
  const source = new SourceFile("test.ts", sourceTextBytes(text));
  const diagnostics = new DiagnosticBag();
  const scanner = new Scanner(source, diagnostics);
  return { tokens: scanner.tokenize(), diagnostics: diagnostics.diagnostics };
}

export function parse(text: string) {
  const source = new SourceFile("test.ts", sourceTextBytes(text));
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

export function compileToIr(
  text: string,
  extensions?: ExtensionRegistry,
  target?: { platform: string; arch: string },
  options: { readonly debug?: boolean } = {},
) {
  const { file, diagnostics: parseDiagnostics } = parse(text);
  const diagnostics = new DiagnosticBag();
  diagnostics.addAll(parseDiagnostics);
  const registry = extensions ?? createDefaultRegistry();
  const { ir, binding } = generate(file, diagnostics, {
    builtins: registry.builtins(),
    modules: registry.modules(),
    ...(target ? { target } : {}),
    debug: options.debug === true,
  });
  return { ir, binding, diagnostics: diagnostics.diagnostics };
}

let cachedClangSupport: boolean | undefined;

/**
 * True when a *supported* clang is available. The end-to-end suites really
 * compile and link native binaries, and the emitted IR needs clang >= 16, so a
 * missing or too-old toolchain skips them instead of failing every case with an
 * opaque-pointer error. `resolveToolchain` applies the same selection (and
 * version gate) the driver uses, so the tests and the compiler agree.
 */
export function hasClang(): boolean {
  if (cachedClangSupport === undefined) {
    try {
      const version = resolveToolchain().version;
      cachedClangSupport = version === undefined || version.major >= MINIMUM_CLANG_MAJOR;
    } catch {
      cachedClangSupport = false;
    }
    if (!cachedClangSupport) {
      console.warn(
        `xbintsc: skipping end-to-end tests — clang ${MINIMUM_CLANG_MAJOR}+ was not found ` +
          "(see doc/requirements.md, or set xbintsc_CLANG)",
      );
    }
  }
  return cachedClangSupport;
}
