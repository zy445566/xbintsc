/**
 * Shared generator state plus the low-level emission primitives.
 *
 * The generator's method groups are installed onto {@link Generator} via
 * `Object.assign`; this context holds the fields they all mutate and the
 * helpers every group relies on.
 */

import { bind, SymbolKind, type BindResult, type ClassInfo, type SymbolInfo } from "../../binder/binder.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { SyntaxKind, type ImportDeclaration, type SourceFileNode, type Node } from "../../ast/nodes.js";
import { i64, XT_UNDEFINED } from "../values.js";
import type {
  BuiltinFunction,
  ExtensionModule,
  ModuleExport,
} from "../../extensions/registry.js";
import type { CodegenOptions, FunctionState } from "./state.js";
import { escapeBytes, kindName, utf8Bytes } from "./tables.js";

export class GeneratorContext {
  readonly binding: BindResult;
  readonly sourceFile: SourceFileNode;
  readonly diagnostics: DiagnosticBag;
  readonly builtins: Readonly<Record<string, BuiltinFunction>>;
  readonly modules: Readonly<Record<string, ExtensionModule>>;
  /** Imported symbol id -> the module binding it refers to. */
  readonly importExports = new Map<number, ModuleExport>();
  /** Imported symbol id -> the namespace it aliases (e.g. `path`). */
  readonly importNamespaces = new Map<number, string>();
  readonly globals: string[] = [];
  readonly functions: string[] = [];
  readonly strings = new Map<string, { label: string; length: number }>();
  readonly classGlobals = new Map<number, string>();
  readonly moduleGlobals = new Map<number, string>();
  readonly extraDeclarations = new Set<string>();
  stringCounter = 0;
  current!: FunctionState;

  constructor(sourceFile: SourceFileNode, diagnostics: DiagnosticBag, options: CodegenOptions = {}) {
    this.sourceFile = sourceFile;
    this.diagnostics = diagnostics;
    this.binding = bind(sourceFile);
    this.builtins = options.builtins ?? {};
    this.modules = options.modules ?? {};
    this.resolveImports();
  }

  /**
   * Map every imported binding to the module binding it names, so calls can be
   * lowered to the extension's runtime symbol and namespace aliases can reuse
   * the namespace dispatch tables.
   */
  private resolveImports(): void {
    if (Object.keys(this.modules).length === 0) return;
    for (const statement of this.sourceFile.statements) {
      if (statement.kind !== SyntaxKind.ImportDeclaration) continue;
      const declaration = statement as ImportDeclaration;
      const module = this.modules[declaration.moduleSpecifier.value];
      if (!module) continue;
      const clause = declaration.importClause;
      if (!clause) continue;
      if (clause.name && module.namespace) {
        const symbol = this.binding.symbolOfDeclaration.get(clause.name);
        if (symbol) this.importNamespaces.set(symbol.id, module.namespace);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          const importedName = specifier.propertyName?.text ?? specifier.name.text;
          const exported = module.exports?.[importedName];
          const symbol = this.binding.symbolOfDeclaration.get(specifier.name);
          if (symbol && exported) this.importExports.set(symbol.id, exported);
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport && module.namespace) {
        const symbol = this.binding.symbolOfDeclaration.get(bindings.name);
        if (symbol) this.importNamespaces.set(symbol.id, module.namespace);
      }
    }
  }

  /** Namespace name an imported alias refers to, if any. */
  namespaceOfSymbol(symbol: SymbolInfo | undefined): string | undefined {
    if (!symbol || symbol.kind !== SymbolKind.Import) return undefined;
    return this.importNamespaces.get(symbol.id);
  }

  // -- emission primitives -------------------------------------------------

  emit(line: string): void {
    this.current.buffer.push(line);
  }

  terminate(line: string): void {
    this.current.buffer.push(`  ${line}`);
    this.current.terminated = true;
  }

  reg(): string {
    return `%r${this.current.reg++}`;
  }

  label(prefix: string): string {
    return `${prefix}.${this.current.label++}`;
  }

  startBlock(label: string): void {
    this.current.buffer.push(`${label}:`);
    this.current.terminated = false;
  }

  alloca(): string {
    const ptr = `%slot${this.current.allocas.length}`;
    this.current.allocas.push(`${ptr} = alloca i64`);
    this.current.escapePointers.push(ptr);
    return ptr;
  }

  const(hex: string): string {
    return i64(hex);
  }

  // -- variables -----------------------------------------------------------

  declareSlot(symbol: SymbolInfo, initial: string): void {
    const globalName = this.moduleGlobals.get(symbol.id);
    if (globalName) {
      if (symbol.boxed) {
        const box = this.reg();
        this.emit(`  ${box} = call i64 @xt_box_new(i64 ${initial})`);
        this.emit(`  store i64 ${box}, i64* ${globalName}`);
      } else {
        this.emit(`  store i64 ${initial}, i64* ${globalName}`);
      }
      this.current.slots.set(symbol.id, { ptr: globalName, boxed: symbol.boxed });
      return;
    }
    const ptr = this.alloca();
    if (symbol.boxed) {
      const box = this.reg();
      this.emit(`  ${box} = call i64 @xt_box_new(i64 ${initial})`);
      this.emit(`  store i64 ${box}, i64* ${ptr}`);
    } else {
      this.emit(`  store i64 ${initial}, i64* ${ptr}`);
    }
    this.current.slots.set(symbol.id, { ptr, boxed: symbol.boxed });
  }

  defineCaptureSlot(symbol: SymbolInfo, box: string): void {
    const ptr = this.alloca();
    this.emit(`  store i64 ${box}, i64* ${ptr}`);
    // A captured symbol is always boxed; the environment holds the box.
    this.current.slots.set(symbol.id, { ptr, boxed: true });
  }

  readSlot(symbol: SymbolInfo): string {
    const globalName = this.moduleGlobals.get(symbol.id);
    if (globalName) {
      const value = this.reg();
      this.emit(`  ${value} = load i64, i64* ${globalName}`);
      if (!symbol.boxed) return value;
      const unboxed = this.reg();
      this.emit(`  ${unboxed} = call i64 @xt_box_get(i64 ${value})`);
      return unboxed;
    }
    const slot = this.current.slots.get(symbol.id);
    if (!slot) {
      // Referenced from an inner function without a capture slot: this should
      // be impossible once the binder has run, but stay defensive.
      this.diagnostics.error(
        DiagnosticCode.CodegenError,
        `Internal: no slot for '${symbol.name}' in ${this.current.fn.name}`,
      );
      return i64(XT_UNDEFINED);
    }
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${slot.ptr}`);
    if (!slot.boxed) return value;
    const unboxed = this.reg();
    this.emit(`  ${unboxed} = call i64 @xt_box_get(i64 ${value})`);
    return unboxed;
  }

  writeSlot(symbol: SymbolInfo, value: string): void {
    const globalName = this.moduleGlobals.get(symbol.id);
    if (globalName) {
      if (!symbol.boxed) {
        this.emit(`  store i64 ${value}, i64* ${globalName}`);
        return;
      }
      const box = this.reg();
      this.emit(`  ${box} = load i64, i64* ${globalName}`);
      this.emit(`  call i64 @xt_box_set(i64 ${box}, i64 ${value})`);
      return;
    }
    const slot = this.current.slots.get(symbol.id);
    if (!slot) {
      this.diagnostics.error(DiagnosticCode.CodegenError, `Internal: no slot for '${symbol.name}'`);
      return;
    }
    if (!slot.boxed) {
      this.emit(`  store i64 ${value}, i64* ${slot.ptr}`);
      return;
    }
    const box = this.reg();
    this.emit(`  ${box} = load i64, i64* ${slot.ptr}`);
    this.emit(`  call i64 @xt_box_set(i64 ${box}, i64 ${value})`);
  }

  // -- string pool ---------------------------------------------------------

  stringValue(text: string): string {
    const entry = this.internString(text);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_string_new(i8* ${entry.label}, i64 ${entry.length})`);
    return result;
  }

  internString(text: string): { label: string; length: number } {
    const existing = this.strings.get(text);
    if (existing) return existing;
    const label = `@.str.${this.stringCounter++}`;
    const bytes = utf8Bytes(text);
    const entry = { label, length: bytes.length };
    this.strings.set(text, entry);
    this.globals.push(`${label} = private unnamed_addr constant [${bytes.length + 1} x i8] c"${escapeBytes(bytes)}\\00"`);
    return entry;
  }

  unsupported(node: Node, what: string): void {
    const label = kindName(node.kind);
    this.diagnostics.error(
      DiagnosticCode.UnsupportedFeature,
      `xbintsc does not yet support this ${what} (${label})`,
      node,
      this.sourceFile.fileName,
    );
  }
}
