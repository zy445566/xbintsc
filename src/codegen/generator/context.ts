/**
 * Shared generator state plus the low-level emission primitives.
 *
 * The generator's method groups are installed onto {@link Generator} via
 * `Object.assign`; this context holds the fields they all mutate and the
 * helpers every group relies on.
 */

import { bind, SymbolKind, type BindResult, type SymbolInfo } from "../../binder/binder.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { SyntaxKind, type Identifier, type ImportDeclaration, type Node, type SourceFileNode } from "../../ast/nodes.js";
import { i64, XT_UNDEFINED } from "../values.js";
import type {
  BuiltinFunction,
  ExtensionModule,
  ModuleExport,
  ModuleExports,
} from "../../extensions/registry.js";
import type { CodegenOptions, FunctionState } from "./state.js";
import { escapeBytes, isErrorFamily, kindName, requiresSetjmpex, utf8Bytes } from "./tables.js";

export class GeneratorContext {
  readonly binding: BindResult;
  readonly sourceFile: SourceFileNode;
  readonly diagnostics: DiagnosticBag;
  readonly builtins: Readonly<Record<string, BuiltinFunction>>;
  readonly modules: Readonly<Record<string, ExtensionModule>>;
  /** Host the emitted IR targets (the process by default; see `CodegenOptions`). */
  readonly target: { readonly platform: string; readonly arch: string };
  /** Imported symbol id -> the module binding it refers to. */
  readonly importExports = new Map<number, ModuleExport>();
  /** Imported symbol id -> the namespace it aliases (e.g. `path`). */
  readonly importNamespaces = new Map<number, string>();
  /**
   * Imported symbol id -> the named exports of the module it aliases, for
   * modules that do not expose a runtime namespace dispatcher (`fs`,
   * `child_process`, `crypto`, ...). Lets `import fs from "node:fs"` lower
   * `fs.readFileSync(...)` straight to the module's runtime symbol.
   */
  readonly importModuleExports = new Map<number, ModuleExports>();
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
    this.target = options.target ?? { platform: process.platform, arch: process.arch };
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
      if (clause.name) {
        const symbol = this.binding.symbolOfDeclaration.get(clause.name);
        if (symbol) this.bindModuleAlias(symbol, module);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          const importedName = specifier.propertyName?.text ?? specifier.name.text;
          const exported = module.exports?.[importedName];
          const symbol = this.binding.symbolOfDeclaration.get(specifier.name);
          if (symbol && exported) {
            this.importExports.set(symbol.id, exported);
            // A named constructor (`import { Buffer } from "buffer"`) also
            // inherits its module's static dispatcher, so `Buffer.from(...)`
            // lowers like `buffer.from(...)`.
            if (exported.isConstructor && module.namespace) {
              this.importNamespaces.set(symbol.id, module.namespace);
            }
          }
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
        const symbol = this.binding.symbolOfDeclaration.get(bindings.name);
        if (symbol) this.bindModuleAlias(symbol, module);
      }
    }
  }

  /**
   * Bind a default/namespace import to either the module's runtime namespace
   * dispatcher (when it has one) or its named exports (when it does not).
   */
  private bindModuleAlias(symbol: SymbolInfo, module: ExtensionModule): void {
    if (module.namespace) {
      this.importNamespaces.set(symbol.id, module.namespace);
    } else if (module.exports) {
      this.importModuleExports.set(symbol.id, module.exports);
    }
  }

  /** Namespace name an imported alias refers to, if any. */
  namespaceOfSymbol(symbol: SymbolInfo | undefined): string | undefined {
    if (!symbol || symbol.kind !== SymbolKind.Import) return undefined;
    return this.importNamespaces.get(symbol.id);
  }

  /** Named exports a default/namespace import aliases, if any. */
  moduleExportsOfSymbol(symbol: SymbolInfo | undefined): ModuleExports | undefined {
    if (!symbol || symbol.kind !== SymbolKind.Import) return undefined;
    return this.importModuleExports.get(symbol.id);
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

  /**
   * Emit the `setjmp` that opens a `try` frame and return the register holding
   * its result. The frame address is always passed explicitly because Windows
   * `_setjmp` stores it in `_JUMP_BUFFER.Frame` for `longjmp` to unwind to; the
   * extra argument is ignored by the one-argument `_setjmp` on Linux/macOS.
   */
  emitSetjmp(frame: string): string {
    if (requiresSetjmpex(this.target.platform, this.target.arch)) {
      // Windows ARM64 has no `_setjmp`; use `_setjmpex`, which expects the
      // caller's stack pointer on entry (`llvm.sponentry`).
      this.extraDeclarations.add("declare i8* @llvm.sponentry()");
      this.extraDeclarations.add("declare i32 @_setjmpex(i8*, i8*) returns_twice");
      const entry = this.reg();
      this.emit(`  ${entry} = call i8* @llvm.sponentry()`);
      const jump = this.reg();
      this.emit(`  ${jump} = call i32 @_setjmpex(i8* ${frame}, i8* ${entry})`);
      return jump;
    }
    const frameAddress = this.reg();
    this.emit(`  ${frameAddress} = call i8* @llvm.frameaddress(i32 0)`);
    const jump = this.reg();
    this.emit(`  ${jump} = call i32 @_setjmp(i8* ${frame}, i8* ${frameAddress})`);
    return jump;
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

  const(value: bigint): string {
    return i64(value);
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

  /**
   * The name of the builtin Error-family constructor an expression refers to
   * (`extends TypeError`), or `undefined` for a user class / shadowed name.
   */
  errorFamilyName(expression: Node): string | undefined {
    if (expression.kind !== SyntaxKind.Identifier) return undefined;
    const identifier = expression as Identifier;
    if (this.binding.symbolOfIdentifier.get(identifier)) return undefined;
    return isErrorFamily(identifier.text) ? identifier.text : undefined;
  }

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
