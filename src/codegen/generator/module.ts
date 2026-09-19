/**
 * Module-level generation: emitting functions, the entry point and the overall
 * IR text, plus default-parameter initialization.
 */

import {
  SyntaxKind,
  type ArrowFunction,
  type Block,
  type Expression,
  type Parameter,
} from "../../ast/nodes.js";
import type { ClassInfo, FunctionInfo, SymbolInfo } from "../../binder/binder.js";
import { SymbolKind } from "../../binder/binder.js";
import { i64, XT_UNDEFINED } from "../values.js";
import { RUNTIME_DECLARATIONS, type FunctionState } from "./state.js";
import type { Generator } from "./generator.js";

export interface ModuleMethods {
  run(this: Generator): string;
  emitFunction(this: Generator, fn: FunctionInfo): void;
  emitMain(this: Generator): void;
  functionName(this: Generator, fn: FunctionInfo): string;
  wrapAsync(this: Generator, value: string): string;
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
        const isVariable = symbol.kind === SymbolKind.Var || symbol.kind === SymbolKind.Let || symbol.kind === SymbolKind.Const;
        if (!isVariable || !symbol.boxed || !symbol.fn.isModule) continue;
        const globalName = `@g.${symbol.id}`;
        this.moduleGlobals.set(symbol.id, globalName);
        this.globals.push(`${globalName} = internal global i64 ${i64(XT_UNDEFINED)}`);
      }
    }
    for (const fn of this.binding.functions) this.emitFunction(fn);
    this.emitMain();
    const header = ["; ModuleID = 'xbintsc'", "source_filename = \"" + this.sourceFile.fileName + "\"", ""];
    return [...header, ...RUNTIME_DECLARATIONS, ...this.extraDeclarations, "", ...this.globals, "", ...this.functions, ""].join("\n");
  },

  // -- module level --------------------------------------------------------

  emitFunction(fn: FunctionInfo): void {
    const name = this.functionName(fn);
    const state: FunctionState = {
      fn,
      buffer: [],
      allocas: [],
      slots: new Map(),
      reg: 0,
      label: 0,
      terminated: false,
      loops: [],
      tryFrames: [],
      escapePointers: [],
      usesTry: false,
    };
    this.current = state;

    const header = `define i64 @${name}(i64 %this, i64 %env, i32 %argc, i64* %argv) {`;
    this.emit(`%saved.env = alloca i64`);
    this.emit(`store i64 %env, i64* %saved.env`);
    this.emit(`%saved.this = alloca i64`);
    this.emit(`store i64 %this, i64* %saved.this`);
    state.thisPtr = "%saved.this";
    this.emit(`%saved.argc = alloca i32`);
    this.emit(`store i32 %argc, i32* %saved.argc`);
    this.emit(`%saved.argv = alloca i64*`);
    this.emit(`store i64* %argv, i64** %saved.argv`);

    // Parameters.
    const parameterNodes = (fn.node as { parameters?: Parameter[] }).parameters ?? [];
    for (let index = 0; index < fn.params.length; index++) {
      const symbol = fn.params[index]!;
      const parameter = parameterNodes[index];
      if (parameter?.dotDotDotToken) {
        const rest = this.reg();
        this.emit(`  ${rest} = call i64 @xt_rest_args(i32 %argc, i64* %argv, i32 ${index})`);
        this.declareSlot(symbol, rest);
        continue;
      }
      const value = this.reg();
      this.emit(`  ${value} = call i64 @xt_arg(i32 %argc, i64* %argv, i32 ${index})`);
      this.declareSlot(symbol, value);
      if (parameter?.initializer) this.emitDefaultParameter(symbol, value, parameter.initializer);
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

    // Body.
    if (fn.node.kind === SyntaxKind.SourceFile) {
      this.emitStatements(this.sourceFile.statements);
    } else if (fn.node.kind === SyntaxKind.ArrowFunction && (fn.node as ArrowFunction).body.kind !== SyntaxKind.Block) {
      const value = this.emitExpression((fn.node as ArrowFunction).body as Expression);
      this.terminate(`ret i64 ${fn.isAsync ? this.wrapAsync(value) : value}`);
    } else {
      const body = (fn.node as { body: Block }).body;
      if (body) this.emitStatements(body.statements);
    }

    if (!this.current.terminated) {
      const fallback = fn.isAsync ? this.wrapAsync(i64(XT_UNDEFINED)) : i64(XT_UNDEFINED);
      this.terminate(`ret i64 ${fallback}`);
    }

    const escapes = state.usesTry
      ? state.escapePointers.map((ptr) => `  call void asm sideeffect "", "r"(i64* ${ptr})`)
      : [];
    const lines = [...state.allocas.map((a) => `  ${a}`), ...escapes, ...state.buffer];
    this.functions.push([header, ...lines, "}", ""].join("\n"));
  },

  emitMain(): void {
    const moduleName = this.functionName(this.binding.moduleFunction);
    this.functions.push(
      [
        "define i32 @main(i32 %argc, i8** %argv) {",
        "  call void @xt_set_program_args(i32 %argc, i8** %argv)",
        `  %result = call i64 @${moduleName}(i64 ${i64(XT_UNDEFINED)}, i64 ${i64(XT_UNDEFINED)}, i32 0, i64* null)`,
        "  call void @xt_drain_microtasks()",
        "  call void @xt_run_event_loop()",
        "  ret i32 0",
        "}",
        "",
      ].join("\n"),
    );
  },

  functionName(fn: FunctionInfo): string {
    return fn.isModule ? "xt_module" : `xt_fn_${fn.id}`;
  },

  /** Wrap a returned value in a resolved promise (for `async` functions). */
  wrapAsync(value: string): string {
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_promise_resolve(i64 ${value})`);
    return result;
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
    if (classInfo.parentExpression) {
      const parent = this.emitExpression(classInfo.parentExpression);
      const parentProto = this.reg();
      this.emit(`  ${parentProto} = call i64 @xt_function_get_prototype(i64 ${parent})`);
      this.emit(`  call i64 @xt_object_set_prototype(i64 ${proto}, i64 ${parentProto})`);
    }
    for (const method of classInfo.methods) {
      const fnValue = this.emitClosureValue(method);
      const key = this.stringValue(method.name);
      this.emit(`  call i64 @xt_set(i64 ${proto}, i64 ${key}, i64 ${fnValue})`);
    }
    const ctor = this.emitClosureValue(classInfo.ctor!);
    this.emit(`  call i64 @xt_function_set_prototype(i64 ${ctor}, i64 ${proto})`);
    const ctorKey = this.stringValue("__ctor");
    this.emit(`  call i64 @xt_set(i64 ${proto}, i64 ${ctorKey}, i64 ${ctor})`);
    for (const method of classInfo.statics) {
      const fnValue = this.emitClosureValue(method);
      const key = this.stringValue(method.name);
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
