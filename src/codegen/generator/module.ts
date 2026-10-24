/**
 * Module-level generation: emitting functions, the entry point and the overall
 * IR text, plus default-parameter initialization.
 */

import {
  ModifierKind,
  SyntaxKind,
  type ArrowFunction,
  type Block,
  type Expression,
  type Parameter,
} from "../../ast/nodes.js";
import type { ClassInfo, FunctionInfo, SymbolInfo } from "../../binder/binder.js";
import { SymbolKind } from "../../binder/binder.js";
import { i64, XT_UNDEFINED } from "../values.js";
import { ARTIFACT_LINE_ENDING, artifactPath } from "../../canonical.js";
import { RUNTIME_DECLARATIONS, type FunctionState } from "./state.js";
import { propertyNameText } from "./tables.js";
import type { Generator } from "./generator.js";

/** Accessibility/`readonly` parameter modifiers make a constructor parameter a property. */
function isParameterProperty(parameter: Parameter): boolean {
  return parameter.modifiers.some(
    (modifier) =>
      modifier.modifierKind === ModifierKind.Public ||
      modifier.modifierKind === ModifierKind.Private ||
      modifier.modifierKind === ModifierKind.Protected ||
      modifier.modifierKind === ModifierKind.Readonly,
  );
}

/** A readable, IR-symbol-safe suffix for a function name. */
function symbolSuffix(name: string): string {
  if (name.length === 0 || name.startsWith("<")) return "";
  return name.replace(/[^A-Za-z0-9_]/g, "_").replace(/^_+/, "").slice(0, 32);
}

export interface ModuleMethods {
  run(this: Generator): string;
  emitFunction(this: Generator, fn: FunctionInfo): void;
  emitFunctionReturn(this: Generator, value: string): void;
  emitMain(this: Generator): void;
  functionName(this: Generator, fn: FunctionInfo): string;
  classGlobal(this: Generator, classInfo: ClassInfo): string;
  emitClassReference(this: Generator, classInfo: ClassInfo): string;
  emitClassSetup(this: Generator, classInfo: ClassInfo): void;
  emitDefaultParameter(this: Generator, symbol: SymbolInfo, value: string, initializer: Expression): void;
}

export const moduleMethods: ModuleMethods = {
  run(): string {
    for (const classInfo of this.binding.classes) {
      const globalName = `@class.${classInfo.id}`;
      this.classGlobals.set(classInfo.id, globalName);
      this.globals.push(`${globalName} = internal global i64 0`);
    }
    // Module-level bindings captured by functions live in globals so that a
    // top-level function can see them without a closure environment.
    const moduleScope = this.binding.scopes.get(this.sourceFile);
    if (moduleScope) {
      for (const symbol of moduleScope.symbols.values()) {
        const isVariable =
          symbol.kind === SymbolKind.Var || symbol.kind === SymbolKind.Let || symbol.kind === SymbolKind.Const;
        const isEnum = symbol.kind === SymbolKind.Enum;
        if ((!isVariable && !isEnum) || !symbol.fn?.isModule) continue;
        if (isVariable && !symbol.boxed) continue;
        const globalName = `@g.${symbol.id}`;
        this.moduleGlobals.set(symbol.id, globalName);
        this.globals.push(`${globalName} = internal global i64 ${i64(XT_UNDEFINED)}`);
      }
    }
    for (const fn of this.binding.functions) this.emitFunction(fn);
    this.emitMain();
    // Path separators are normalised to `/` so the emitted IR (and therefore
    // the self-hosting fixpoint) is identical whether the compiler runs on
    // Node (which uses `\` on Windows) or as a compiled binary (which always
    // produces `/`).
    const sourceName = artifactPath(this.sourceFile.fileName);
    const header = [
      "; ModuleID = 'xbintsc'",
      "source_filename = \"" + sourceName + "\"",
      "",
    ];
    const runtimeDeclarations = new Set(RUNTIME_DECLARATIONS);
    const extraDeclarations = [...this.extraDeclarations].filter((line) => !runtimeDeclarations.has(line));
    return [
      ...header,
      ...RUNTIME_DECLARATIONS,
      ...extraDeclarations,
      "",
      ...this.globals,
      "",
      ...this.functions,
      "",
      ...this.debugMetadataLines(),
    ].join(ARTIFACT_LINE_ENDING);
  },

  // -- module level --------------------------------------------------------

  emitFunction(fn: FunctionInfo): void {
    this.unsupportedCount = 0;
    const name = this.functionName(fn);
    const debugSubprogram = this.debugSubprogramFor(fn);
    const state: FunctionState = {
      fn,
      buffer: [],
      allocas: [],
      slots: new Map(),
      reg: 0,
      label: 0,
      terminated: false,
      loops: [],
      pendingLabels: [],
      tryFrames: [],
      finallyStack: [],
      escapePointers: [],
      usesTry: false,
      /* Generator and async bodies run on a private coroutine stack that can be
       * suspended in the middle of a call. Their shadow frame would then sit on
       * the shadow stack while the caller unwinds its own, so leave them out of
       * `Error.stack` frames (nesting calls inside them are still recorded). */
      trackFrames: !fn.isGenerator && !fn.isAsync,
      ...(debugSubprogram ? { debugSubprogram } : {}),
    };
    this.current = state;
    if (debugSubprogram) this.setDebugLocation(fn.node);

    const debugSuffix = debugSubprogram ? ` !dbg ${debugSubprogram}` : "";
    const header = `define i64 @${name}(i64 %this, i64 %env, i32 %argc, i64* %argv)${debugSuffix} {`;
    this.emit(`%saved.env = alloca i64`);
    this.emit(`store i64 %env, i64* %saved.env`);
    this.emit(`%saved.this = alloca i64`);
    this.emit(`store i64 %this, i64* %saved.this`);
    state.thisPtr = "%saved.this";
    this.emit(`%saved.argc = alloca i32`);
    this.emit(`store i32 %argc, i32* %saved.argc`);
    this.emit(`%saved.argv = alloca i64*`);
    this.emit(`store i64* %argv, i64** %saved.argv`);

    // Shadow-stack frame: `Error.stack` and fatal-signal dumps walk this.
    if (state.trackFrames) {
      const displayName = fn.isModule
        ? "module"
        : fn.isConstructor && fn.classInfo
          ? `new ${this.displayNameOf(fn.classInfo.name)}`
          : this.displayNameOf(fn.name.length > 0 ? fn.name : "anonymous");
      const namePointer = this.internStatic(displayName);
      /* Normalise separators like `source_filename`: the compiler may run under
       * Node (`\`) or as a self-hosted binary (`/`), and the IR must stay
       * byte-identical across the two. */
      const filePointer = this.internStatic(artifactPath(this.fileNameOf(fn.node)));
      this.emit(
        `  call void @xt_stack_push(i8* ${namePointer}, i8* ${filePointer}, i32 ${this.lineOf(fn.node)})`,
      );
    }

    // Parameters.
    const parameterNodes = (fn.node as { parameters?: Parameter[] }).parameters ?? [];
    for (let index = 0; index < fn.params.length; index++) {
      const symbol = fn.params[index]!;
      const parameter = parameterNodes[index];
      if (!parameter) continue;
      if (parameter.dotDotDotToken) {
        const rest = this.reg();
        this.emit(`  ${rest} = call i64 @xt_rest_args(i32 %argc, i64* %argv, i32 ${index})`);
        this.declareSlot(symbol, rest);
        continue;
      }
      let value = this.reg();
      this.emit(`  ${value} = call i64 @xt_arg(i32 %argc, i64* %argv, i32 ${index})`);
      if (parameter.name.kind !== SyntaxKind.Identifier) {
        // A destructuring parameter (`([a, b]) => ...` / `({ a }) => ...`) binds
        // every name introduced by its pattern, not just the first one.
        if (parameter.initializer) value = this.emitBindingDefault(value, parameter.initializer);
        this.emitBindingPattern(parameter.name, value);
        continue;
      }
      this.declareSlot(symbol, value);
      if (parameter.initializer) this.emitDefaultParameter(symbol, value, parameter.initializer);
    }

    // Captures threaded through the environment.
    for (const symbol of fn.captures) {
      if (this.moduleGlobals.has(symbol.id)) continue;
      const index = fn.captureIndex.get(symbol.id)!;
      const value = this.reg();
      this.emit(`  ${value} = call i64 @xt_closure_env(i64 %env, i32 ${index})`);
      this.defineCaptureSlot(symbol, value);
    }

    // Arrow functions inherit `this` lexically; it travels as an extra capture.
    if (fn.isArrow && fn.capturesThis) {
      const inherited = this.reg();
      this.emit(`  ${inherited} = call i64 @xt_closure_env(i64 %env, i32 ${fn.captures.length})`);
      this.emit(`  store i64 ${inherited}, i64* %saved.this`);
    }

    // A derived class with no explicit constructor gets an implicit one that
    // forwards every argument to the parent constructor (`super(...arguments)`).
    if (fn.isConstructor && fn.classInfo?.parentExpression) {
      const explicitBody = (fn.node as { body?: Block }).body;
      const parent = fn.classInfo.parentExpression;
      const errorName = this.errorFamilyName(parent);
      if (!explicitBody && !errorName) {
        const parentValue = this.emitExpression(parent);
        const parentProto = this.reg();
        this.emit(`  ${parentProto} = call i64 @xt_function_get_prototype(i64 ${parentValue})`);
        const ctorKey = this.stringValue("__ctor");
        const parentCtor = this.reg();
        this.emit(`  ${parentCtor} = call i64 @xt_get(i64 ${parentProto}, i64 ${ctorKey})`);
        const superThis = this.emitThis();
        this.emit(`  call i64 @xt_call_with_this(i64 ${parentCtor}, i64 ${superThis}, i32 %argc, i64* %argv)`);
      } else if (!explicitBody && errorName) {
        // `extends Error` / `extends TypeError` with no explicit constructor:
        // initialise the instance's name and message from the arguments.
        const superThis = this.emitThis();
        const nameValue = this.stringValue(errorName);
        this.emit(`  call i64 @xt_error_init(i64 ${superThis}, i64 ${nameValue}, i32 %argc, i64* %argv)`);
      }
    }

    // Instance field initializers run before the constructor body.
    if (fn.isConstructor && fn.classInfo) {
      const thisValue = this.emitThis();
      for (const field of fn.classInfo.fields) {
        if (field.isStatic) continue;
        const key = this.stringValue(field.name);
        const value = field.initializer ? this.emitExpression(field.initializer) : i64(XT_UNDEFINED);
        this.emit(`  call i64 @xt_set(i64 ${thisValue}, i64 ${key}, i64 ${value})`);
      }
    }

    // Constructor parameter properties (`constructor(private readonly x: T) {}`)
    // copy the incoming argument onto `this`.
    if (fn.isConstructor) {
      const thisValue = this.emitThis();
      for (let index = 0; index < fn.params.length; index++) {
        const parameter = parameterNodes[index];
        if (!parameter || !isParameterProperty(parameter)) continue;
        const symbol = fn.params[index]!;
        const key = this.stringValue(propertyNameText(parameter.name));
        const value = this.readSlot(symbol);
        this.emit(`  call i64 @xt_set(i64 ${thisValue}, i64 ${key}, i64 ${value})`);
      }
    }

    // Body.
    if (fn.node.kind === SyntaxKind.SourceFile) {
      this.emitStatements(this.sourceFile.statements);
    } else if (fn.node.kind === SyntaxKind.ArrowFunction && (fn.node as ArrowFunction).body.kind !== SyntaxKind.Block) {
      const value = this.emitExpression((fn.node as ArrowFunction).body as Expression);
      this.emitFunctionReturn(value);
    } else {
      const body = (fn.node as { body: Block }).body;
      if (body) this.emitStatements(body.statements);
    }

    if (!this.current.terminated) {
      /* An `async` body returns its value plainly: the runtime's async
       * coroutine settles the returned promise (and adopts a returned
       * promise), so wrapping here would add a redundant promise. */
      this.emitFunctionReturn(i64(XT_UNDEFINED));
    }

    const escapes = state.usesTry
      ? state.escapePointers.map((ptr) => `  call void asm sideeffect "", "r"(i64* ${ptr})`)
      : [];
    const lines = [...state.allocas.map((a) => `  ${a}`), ...escapes, ...state.buffer];
    this.functions.push([header, ...lines, "}", ""].join(ARTIFACT_LINE_ENDING));
  },

  emitMain(): void {
    const moduleName = this.functionName(this.binding.moduleFunction);
    // Module bindings and class constructors live in globals; register them as
    // GC roots before the module body runs so a collection cannot free them.
    const stringEntries = [...this.strings.values()];
    const roots = [
      ...this.classGlobals.values(),
      ...this.moduleGlobals.values(),
      ...stringEntries.map((entry) => entry.slot),
    ];
    const rootLines = roots.map((name) => `  call void @xt_gc_add_root(i64* ${name})`);
    // Every interned string literal is materialised once here and shared by all
    // uses, so property names and other literals stop allocating per access.
    const stringInit = stringEntries.flatMap((entry) => {
      const register = `%strv.${entry.slot.replace("@.strv.", "")}`;
      return [
        `  ${register} = call i64 @xt_string_new(i8* ${entry.label}, i64 ${entry.length})`,
        `  store i64 ${register}, i64* ${entry.slot}`,
      ];
    });
    this.functions.push(
      [
        "define i32 @main(i32 %argc, i8** %argv) {",
        "  %stackbase = alloca i64",
        "  %stackbase.ptr = bitcast i64* %stackbase to i8*",
        "  call void @xt_gc_set_stack_base(i8* %stackbase.ptr)",
        "  call void @xt_gc_init()",
        ...rootLines,
        ...stringInit,
        "  call void @xt_gc_arm()",
        "  call void @xt_set_program_args(i32 %argc, i8** %argv)",
        `  %result = call i64 @${moduleName}(i64 ${i64(XT_UNDEFINED)}, i64 ${i64(XT_UNDEFINED)}, i32 0, i64* null)`,
        "  call void @xt_drain_microtasks()",
        "  call void @xt_run_event_loop()",
        "  ret i32 0",
        "}",
        "",
      ].join(ARTIFACT_LINE_ENDING),
    );
  },

  functionName(fn: FunctionInfo): string {
    if (fn.isModule) return "xt_module";
    /* Keep the original name in the symbol so native debuggers, crash
     * backtraces and `--emit ir` are readable (`xt_fn_3_sum`). */
    const suffix = symbolSuffix(this.displayNameOf(fn.name));
    return suffix.length > 0 ? `xt_fn_${fn.id}_${suffix}` : `xt_fn_${fn.id}`;
  },

  /** Emit the function's return, dropping its shadow-stack frame first. */
  emitFunctionReturn(value: string): void {
    if (this.current.trackFrames) this.emit("  call void @xt_stack_pop()");
    this.terminate(`ret i64 ${value}`);
  },

  classGlobal(classInfo: ClassInfo): string {
    const existing = this.classGlobals.get(classInfo.id);
    if (existing) return existing;
    const name = `@class.${classInfo.id}`;
    this.classGlobals.set(classInfo.id, name);
    this.globals.push(`${name} = internal global i64 0`);
    return name;
  },

  emitClassReference(classInfo: ClassInfo): string {
    const globalName = this.classGlobal(classInfo);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${globalName}`);
    return value;
  },

  /** Build a class value (constructor closure + prototype) and store it in its global. */
  emitClassSetup(classInfo: ClassInfo): void {
    const globalName = this.classGlobal(classInfo);
    const proto = this.reg();
    this.emit(`  ${proto} = call i64 @xt_object_new()`);
    /* The parent constructor value, emitted once: it is used both for the
     * instance prototype and for static inheritance (`Dog.kind` sees
     * `Animal.kind`), and evaluating `extends mixin()` twice would run the
     * expression twice. */
    let parentValue: string | undefined;
    if (classInfo.parentExpression) {
      // `extends Error` / `extends TypeError` / other host builtins: chain the
      // subclass prototype onto the builtin's prototype so `instanceof` works.
      const parent = classInfo.parentExpression;
      const errorName = this.errorFamilyName(parent);
      parentValue = this.emitExpression(parent);
      if (errorName) {
        const nameValue = this.stringValue(errorName);
        const parentProto = this.reg();
        this.emit(`  ${parentProto} = call i64 @xt_error_prototype(i64 ${nameValue})`);
        this.emit(`  call i64 @xt_object_set_prototype(i64 ${proto}, i64 ${parentProto})`);
      } else {
        const parentProto = this.reg();
        this.emit(`  ${parentProto} = call i64 @xt_function_get_prototype(i64 ${parentValue})`);
        this.emit(`  call i64 @xt_object_set_prototype(i64 ${proto}, i64 ${parentProto})`);
      }
    }
    for (const method of classInfo.methods) {
      const fnValue = this.emitClosureValue(method);
      const key = method.computedKey
        ? this.emitExpression(method.computedKey)
        : this.stringValue(method.name);
      const accessor = (method.node as { accessor?: "get" | "set" }).accessor;
      if (accessor === "get") {
        this.emit(`  call i64 @xt_object_define_getter(i64 ${proto}, i64 ${key}, i64 ${fnValue})`);
      } else if (accessor === "set") {
        this.emit(`  call i64 @xt_object_define_setter(i64 ${proto}, i64 ${key}, i64 ${fnValue})`);
      } else {
        this.emit(`  call i64 @xt_set(i64 ${proto}, i64 ${key}, i64 ${fnValue})`);
      }
    }
    const ctor = this.emitClosureValue(classInfo.ctor!);
    this.emit(`  call i64 @xt_function_set_prototype(i64 ${ctor}, i64 ${proto})`);
    /* Static members are inherited through the constructor's property bag. */
    if (parentValue) {
      this.emit(`  call i64 @xt_object_set_prototype(i64 ${ctor}, i64 ${parentValue})`);
    }
    const ctorKey = this.stringValue("__ctor");
    this.emit(`  call i64 @xt_set(i64 ${proto}, i64 ${ctorKey}, i64 ${ctor})`);
    for (const method of classInfo.statics) {
      const fnValue = this.emitClosureValue(method);
      const key = method.computedKey
        ? this.emitExpression(method.computedKey)
        : this.stringValue(method.name);
      this.emit(`  call i64 @xt_set(i64 ${ctor}, i64 ${key}, i64 ${fnValue})`);
    }
    for (const field of classInfo.fields) {
      if (!field.isStatic) continue;
      const key = this.stringValue(field.name);
      const value = field.initializer ? this.emitExpression(field.initializer) : i64(XT_UNDEFINED);
      this.emit(`  call i64 @xt_set(i64 ${ctor}, i64 ${key}, i64 ${value})`);
    }
    this.emit(`  store i64 ${ctor}, i64* ${globalName}`);
  },

  emitDefaultParameter(symbol: SymbolInfo, value: string, initializer: Expression): void {
    const isUndefined = this.reg();
    this.emit(`  ${isUndefined} = call i64 @xt_seq(i64 ${value}, i64 ${i64(XT_UNDEFINED)})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${isUndefined})`);
    const condition = this.reg();
    this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    const applyLabel = this.label("param.default");
    const endLabel = this.label("param.end");
    this.terminate(`br i1 ${condition}, label %${applyLabel}, label %${endLabel}`);
    this.startBlock(applyLabel);
    const fallback = this.emitExpression(initializer);
    this.writeSlot(symbol, fallback);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
  },
};
