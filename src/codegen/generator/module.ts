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
import type { FunctionInfo, SymbolInfo } from "../../binder/binder.js";
import { i64, XT_UNDEFINED } from "../values.js";
import { RUNTIME_DECLARATIONS, type FunctionState } from "./state.js";
import type { Generator } from "./generator.js";

export interface ModuleMethods {
  run(this: Generator): string;
  emitFunction(this: Generator, fn: FunctionInfo): void;
  emitMain(this: Generator): void;
  functionName(this: Generator, fn: FunctionInfo): string;
  emitDefaultParameter(this: Generator, symbol: SymbolInfo, value: string, initializer: Expression): void;
}

export const moduleMethods: ModuleMethods = {
  run(): string {
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

    const header = `define i64 @${name}(i64 %env, i32 %argc, i64* %argv) {`;
    this.emit(`%saved.env = alloca i64`);
    this.emit(`store i64 %env, i64* %saved.env`);
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
      const index = fn.captureIndex.get(symbol.id)!;
      const value = this.reg();
      this.emit(`  ${value} = call i64 @xt_closure_env(i64 %env, i32 ${index})`);
      this.defineCaptureSlot(symbol, value);
    }

    // Body.
    if (fn.node.kind === SyntaxKind.SourceFile) {
      this.emitStatements(this.sourceFile.statements);
    } else if (fn.node.kind === SyntaxKind.ArrowFunction && (fn.node as ArrowFunction).body.kind !== SyntaxKind.Block) {
      const value = this.emitExpression((fn.node as ArrowFunction).body as Expression);
      this.terminate(`ret i64 ${value}`);
    } else {
      const body = (fn.node as { body: Block }).body;
      this.emitStatements(body.statements);
    }

    if (!this.current.terminated) this.terminate(`ret i64 ${i64(XT_UNDEFINED)}`);

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
        `  %result = call i64 @${moduleName}(i64 ${i64(XT_UNDEFINED)}, i32 0, i64* null)`,
        "  ret i32 0",
        "}",
        "",
      ].join("\n"),
    );
  },

  functionName(fn: FunctionInfo): string {
    return fn.isModule ? "xt_module" : `xt_fn_${fn.id}`;
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
