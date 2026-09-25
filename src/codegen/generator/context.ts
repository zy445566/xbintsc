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
  /** Module specifier -> extension name for known but unregistered extensions. */
  readonly moduleHints: Readonly<Record<string, string>>;
  /**
   * Imported symbols whose providing module was already reported as missing
   * (hint emitted). Referencing them is skipped so the actionable hint is the
   * only diagnostic, instead of a second "cannot be used as a value" error.
   */
  readonly missingModuleSymbols = new Set<number>();
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
  /**
   * Imported symbol id -> the module's `default` export, for callable default
   * imports such as `import test from "node:test"`.
   */
  readonly importDefaults = new Map<number, ModuleExport>();
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
    this.moduleHints = options.moduleHints ?? {};
    this.target = options.target ?? { platform: process.platform, arch: process.arch };
    this.resolveImports();
  }

  /**
   * Map every imported binding to the module binding it names, so calls can be
   * lowered to the extension's runtime symbol and namespace aliases can reuse
   * the namespace dispatch tables.
   */
  private resolveImports(): void {
    for (const statement of this.sourceFile.statements) {
      if (statement.kind !== SyntaxKind.ImportDeclaration) continue;
      const declaration = statement as ImportDeclaration;
      const clause = declaration.importClause;
      // Type-only imports are erased at runtime, so a missing host module is
      // not an error for them.
      if (!clause || clause.isTypeOnly) continue;
      const specifier = declaration.moduleSpecifier.value;
      const module = this.modules[specifier];
      if (!module) {
        this.reportMissingModule(declaration, specifier);
        continue;
      }
      if (clause.name) {
        const symbol = this.binding.symbolOfDeclaration.get(clause.name);
        if (symbol) {
          this.bindModuleAlias(symbol, module);
          const defaultExport = module.exports?.["default"];
          if (defaultExport) this.importDefaults.set(symbol.id, defaultExport);
        }
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifierNode of bindings.elements) {
          if (specifierNode.isTypeOnly) continue;
          const importedName = specifierNode.propertyName?.text ?? specifierNode.name.text;
          const exported = module.exports?.[importedName];
          const symbol = this.binding.symbolOfDeclaration.get(specifierNode.name);
          if (!symbol) continue;
          if (exported) {
            this.importExports.set(symbol.id, exported);
            // A named constructor (`import { Buffer } from "buffer"`) also
            // inherits its module's static dispatcher, so `Buffer.from(...)`
            // lowers like `buffer.from(...)`.
            if (exported.isConstructor && module.namespace) {
              this.importNamespaces.set(symbol.id, module.namespace);
            }
          } else if (this.usedAsValue(symbol)) {
            this.diagnostics.error(
              DiagnosticCode.ModuleNotFound,
              `Module '"${specifier}"' has no exported member '${importedName}'`,
              specifierNode.name,
              this.sourceFile.fileName,
            );
            this.missingModuleSymbols.add(symbol.id);
          }
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
        const symbol = this.binding.symbolOfDeclaration.get(bindings.name);
        if (symbol) this.bindModuleAlias(symbol, module);
      }
    }
  }

  /**
   * Diagnose an import of a module the registry cannot provide.
   *
   * A *hinted* module belongs to an extension the caller knows about but did
   * not enable, so point at the flag that enables it. Anything else is a bare
   * specifier xbintsc cannot link - almost always a third-party package from
   * `node_modules` - and must be reported at the import site rather than
   * degrading into a downstream "cannot be used as a value" error.
   */
  private reportMissingModule(declaration: ImportDeclaration, specifier: string): void {
    const provider = this.moduleHints[specifier];
    if (provider) {
      this.diagnostics.error(
        DiagnosticCode.ModuleNotFound,
        `module '${specifier}' is provided by the '${provider}' extension; pass --ext ${provider}`,
        declaration.moduleSpecifier,
        this.sourceFile.fileName,
      );
      this.markMissingModuleSymbols(declaration);
      return;
    }
    // A path that is not a bare specifier is a bundling concern; the driver
    // already reports unresolved relative imports, so stay out of the way.
    if (!isBareSpecifier(specifier)) return;
    // Only report when a binding is actually read: a type-only use is erased
    // by the binder and must keep compiling.
    const symbols = this.importBindingSymbols(declaration);
    if (!symbols.some((symbol) => this.usedAsValue(symbol))) return;
    this.diagnostics.error(
      DiagnosticCode.ModuleNotFound,
      `module '${specifier}' is not supported: xbintsc can import built-in platform modules, ` +
        "relative '.ts' files and ESM packages under node_modules; CommonJS packages are not supported",
      declaration.moduleSpecifier,
      this.sourceFile.fileName,
    );
    for (const symbol of symbols) this.missingModuleSymbols.add(symbol.id);
  }

  /**
   * True when a binding is read as a runtime value. Type positions are not
   * bound (the binder skips them), so a symbol with no references is erased.
   */
  private usedAsValue(symbol: SymbolInfo): boolean {
    return symbol.references.length > 0;
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

  /** Every binding an import declaration introduces (including type-only ones). */
  private importBindingSymbols(declaration: ImportDeclaration): SymbolInfo[] {
    const clause = declaration.importClause;
    if (!clause) return [];
    const names: Identifier[] = [];
    if (clause.name) names.push(clause.name);
    const bindings = clause.namedBindings;
    if (bindings && bindings.kind === SyntaxKind.NamedImports) {
      for (const specifier of bindings.elements) names.push(specifier.name);
    } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
      names.push(bindings.name);
    }
    const symbols: SymbolInfo[] = [];
    for (const name of names) {
      const symbol = this.binding.symbolOfDeclaration.get(name);
      if (symbol) symbols.push(symbol);
    }
    return symbols;
  }

  /** Record every symbol introduced by an import of a missing known module. */
  private markMissingModuleSymbols(declaration: ImportDeclaration): void {
    for (const symbol of this.importBindingSymbols(declaration)) this.missingModuleSymbols.add(symbol.id);
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

  /**
   * Report a CommonJS `require(...)` use. xbintsc has no CommonJS module
   * runtime, so point the user at the ESM `import` form (with the requested
   * module in the hint when it is a string literal).
   */
  reportRequireUse(node: Node): void {
    const specifier = requireSpecifier(node);
    const replacement = specifier
      ? `use an ESM import instead, e.g. \`import value from ${JSON.stringify(specifier)}\``
      : "use an ESM `import` statement instead";
    this.diagnostics.error(
      DiagnosticCode.UnsupportedFeature,
      `CommonJS \`require()\` is not supported; ${replacement}`,
      node,
      this.sourceFile.fileName,
    );
  }
}

/** The string literal passed to `require("...")`, when there is one. */
function requireSpecifier(node: Node): string | undefined {
  if (node.kind !== SyntaxKind.CallExpression) return undefined;
  const first = (node as { arguments?: readonly Node[] }).arguments?.[0];
  if (first && first.kind === SyntaxKind.StringLiteral) {
    return (first as unknown as { value: string }).value;
  }
  return undefined;
}

/** A bare module specifier (`fs`, `node:fs`, `@scope/pkg`), not a file path. */
function isBareSpecifier(specifier: string): boolean {
  return (
    !specifier.startsWith(".") &&
    !specifier.startsWith("/") &&
    !specifier.startsWith("\\") &&
    !/^[A-Za-z]:[\\/]/.test(specifier)
  );
}
