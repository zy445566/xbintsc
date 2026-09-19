/**
 * LLVM IR code generation.
 *
 * The generator lowers the bound AST to textual LLVM IR. It follows three
 * rules that keep the output simple and correct without a register allocator
 * of our own:
 *
 *   1. Every value crossing a statement or block boundary lives in an
 *      `alloca` (emitted in the entry block so LLVM's mem2reg can promote it).
 *   2. Control flow is expressed with explicit basic blocks; conditionals and
 *      short-circuit operators materialise into a temporary slot instead of
 *      `phi` nodes, which the optimizer folds away.
 *   3. All JavaScript semantics (addition with coercion, comparisons, member
 *      lookup, ...) are delegated to the C runtime through `@xt_*` calls.
 *
 * Later pipeline stages (optimization passes, object emission) live in the
 * driver; this module only produces IR text.
 */

import {
  SyntaxKind,
  type Node,
  type SourceFileNode,
  type Expression,
  type Statement,
  type Identifier,
  type CallExpression,
  type PropertyAccessExpression,
  type ElementAccessExpression,
  type BinaryExpression,
  type ConditionalExpression,
  type ArrowFunction,
  type FunctionExpression,
  type FunctionDeclaration,
  type VariableStatement,
  type VariableDeclarationList,
  type Block,
  type IfStatement,
  type WhileStatement,
  type DoStatement,
  type ForStatement,
  type ForOfStatement,
  type ForInStatement,
  type SwitchStatement,
  type CaseClause,
  type TryStatement,
  type DeleteExpression,
  type ReturnStatement,
  type ThrowStatement,
  type ObjectLiteralExpression,
  type ArrayLiteralExpression,
  type TemplateLiteral,
  type PrefixUnaryExpression,
  type PostfixUnaryExpression,
  type Parameter,
} from "../ast/nodes.js";
import {
  AssignmentOperator,
  BinaryOperator,
  PrefixUnaryOperator,
  PostfixUnaryOperator,
} from "../ast/nodes.js";
import {
  bind,
  type BindResult,
  type FunctionInfo,
  type SymbolInfo,
  SymbolKind,
} from "../binder/binder.js";
import type { DiagnosticBag } from "../diagnostics/diagnostic.js";
import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import type { BuiltinFunction } from "../extensions/registry.js";
import { i64, numberLiteral, XT_UNDEFINED, XT_NULL, XT_FALSE, XT_TRUE } from "./values.js";

interface Slot {
  /** Register holding the `i64*` to the storage. */
  readonly ptr: string;
  readonly boxed: boolean;
}

interface LoopLabels {
  readonly breakLabel: string;
  readonly continueLabel: string;
  /** Number of enclosing `try` frames when the loop was entered. */
  readonly tryDepth?: number;
}

interface FunctionState {
  readonly fn: FunctionInfo;
  readonly buffer: string[];
  readonly allocas: string[];
  readonly slots: Map<number, Slot>;
  reg: number;
  label: number;
  terminated: boolean;
  readonly loops: LoopLabels[];
  readonly tryFrames: string[];
  readonly escapePointers: string[];
  /** Set when the function contains a `try`, forcing locals to live in memory. */
  usesTry: boolean;
}

const RUNTIME_DECLARATIONS: readonly string[] = [
  "declare i64 @xt_undefined()",
  "declare i64 @xt_null()",
  "declare i64 @xt_bool(i32)",
  "declare i64 @xt_number(double)",
  "declare i64 @xt_string_new(i8*, i64)",
  "declare i64 @xt_string_from_cstr(i8*)",
  "declare i64 @xt_arg(i32, i64*, i32)",
  "declare i64 @xt_closure_new(i8*, i32, i64*)",
  "declare i64 @xt_closure_call(i64, i32, i64*)",
  "declare i64 @xt_closure_env(i64, i32)",
  "declare i32 @xt_truthy(i64)",
  "declare double @xt_to_number(i64)",
  "declare i64 @xt_to_string(i64)",
  "declare i64 @xt_typeof(i64)",
  "declare i64 @xt_add(i64, i64)",
  "declare i64 @xt_sub(i64, i64)",
  "declare i64 @xt_mul(i64, i64)",
  "declare i64 @xt_div(i64, i64)",
  "declare i64 @xt_mod(i64, i64)",
  "declare i64 @xt_pow(i64, i64)",
  "declare i64 @xt_neg(i64)",
  "declare i64 @xt_pos(i64)",
  "declare i64 @xt_bit_and(i64, i64)",
  "declare i64 @xt_bit_or(i64, i64)",
  "declare i64 @xt_bit_xor(i64, i64)",
  "declare i64 @xt_bit_not(i64)",
  "declare i64 @xt_shl(i64, i64)",
  "declare i64 @xt_shr(i64, i64)",
  "declare i64 @xt_ushr(i64, i64)",
  "declare i64 @xt_lt(i64, i64)",
  "declare i64 @xt_le(i64, i64)",
  "declare i64 @xt_gt(i64, i64)",
  "declare i64 @xt_ge(i64, i64)",
  "declare i64 @xt_eq(i64, i64)",
  "declare i64 @xt_ne(i64, i64)",
  "declare i64 @xt_seq(i64, i64)",
  "declare i64 @xt_sne(i64, i64)",
  "declare i64 @xt_not(i64)",
  "declare i64 @xt_object_new()",
  "declare i64 @xt_object_get_cstr(i64, i8*)",
  "declare i64 @xt_object_set(i64, i64, i64)",
  "declare i64 @xt_object_has(i64, i64)",
  "declare i64 @xt_object_keys(i64)",
  "declare i64 @xt_object_values(i64)",
  "declare i64 @xt_object_entries(i64)",
  "declare i64 @xt_object_assign(i32, i64*)",
  "declare i64 @xt_object_spread(i64, i64)",
  "declare i64 @xt_call_method(i64, i64, i32, i64*)",
  "declare i64 @xt_math_call(i64, i32, i64*)",
  "declare i64 @xt_parse_int(i32, i64*)",
  "declare i64 @xt_parse_float(i32, i64*)",
  "declare i64 @xt_is_nan(i32, i64*)",
  "declare i64 @xt_is_finite(i32, i64*)",
  "declare i64 @xt_number_ctor(i32, i64*)",
  "declare i64 @xt_string_ctor(i32, i64*)",
  "declare i64 @xt_boolean_ctor(i32, i64*)",
  "declare i64 @xt_in(i64, i64)",
  "declare i64 @xt_delete(i64, i64)",
  "declare i64 @xt_rest_args(i32, i64*, i32)",
  "declare i64 @xt_array_new(i32, i64*)",
  "declare i64 @xt_array_push(i64, i64)",
  "declare i64 @xt_array_length(i64)",
  "declare i64 @xt_array_spread(i64, i64)",
  "declare i64 @xt_get(i64, i64)",
  "declare i64 @xt_set(i64, i64, i64)",
  "declare i64 @xt_box_new(i64)",
  "declare i64 @xt_box_get(i64)",
  "declare i64 @xt_box_set(i64, i64)",
  "declare i64 @xt_is_nullish(i64)",
  "declare void @xt_throw(i64)",
  "declare i32 @_setjmp(i8*) returns_twice",
  "declare i8* @xt_try_enter()",
  "declare i64 @xt_try_exception(i8*)",
  "declare void @xt_try_leave(i8*)",
  "declare void @xt_console_log(i32, i64*)",
  "declare void @xt_console_info(i32, i64*)",
  "declare void @xt_console_warn(i32, i64*)",
  "declare void @xt_console_error(i32, i64*)",
];

export interface CodegenResult {
  readonly ir: string;
  readonly binding: BindResult;
}

export interface CodegenOptions {
  /** Global names supplied by registered extensions. */
  readonly builtins?: Readonly<Record<string, BuiltinFunction>>;
}

export function generate(
  sourceFile: SourceFileNode,
  diagnostics: DiagnosticBag,
  options: CodegenOptions = {},
): CodegenResult {
  const generator = new Generator(sourceFile, diagnostics, options);
  return { ir: generator.run(), binding: generator.binding };
}

class Generator {
  readonly binding: BindResult;
  private readonly globals: string[] = [];
  private readonly functions: string[] = [];
  private readonly strings = new Map<string, { label: string; length: number }>();
  private readonly extraDeclarations = new Set<string>();
  private readonly builtins: Readonly<Record<string, BuiltinFunction>>;
  private stringCounter = 0;
  private current!: FunctionState;

  constructor(
    private readonly sourceFile: SourceFileNode,
    private readonly diagnostics: DiagnosticBag,
    options: CodegenOptions = {},
  ) {
    this.binding = bind(sourceFile);
    this.builtins = options.builtins ?? {};
  }

  run(): string {
    for (const fn of this.binding.functions) this.emitFunction(fn);
    this.emitMain();
    const header = ["; ModuleID = 'xbintsc'", "source_filename = \"" + this.sourceFile.fileName + "\"", ""];
    return [...header, ...RUNTIME_DECLARATIONS, ...this.extraDeclarations, "", ...this.globals, "", ...this.functions, ""].join("\n");
  }

  // -- module level --------------------------------------------------------

  private emitFunction(fn: FunctionInfo): void {
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
  }

  private emitMain(): void {
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
  }

  private functionName(fn: FunctionInfo): string {
    return fn.isModule ? "xt_module" : `xt_fn_${fn.id}`;
  }

  // -- emission primitives -------------------------------------------------

  private emit(line: string): void {
    this.current.buffer.push(line);
  }

  private terminate(line: string): void {
    this.current.buffer.push(`  ${line}`);
    this.current.terminated = true;
  }

  private reg(): string {
    return `%r${this.current.reg++}`;
  }

  private label(prefix: string): string {
    return `${prefix}.${this.current.label++}`;
  }

  private startBlock(label: string): void {
    this.current.buffer.push(`${label}:`);
    this.current.terminated = false;
  }

  private alloca(): string {
    const ptr = `%slot${this.current.allocas.length}`;
    this.current.allocas.push(`${ptr} = alloca i64`);
    this.current.escapePointers.push(ptr);
    return ptr;
  }

  private const(hex: bigint): string {
    return i64(hex);
  }

  // -- variables -----------------------------------------------------------

  private declareSlot(symbol: SymbolInfo, initial: string): void {
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

  private defineCaptureSlot(symbol: SymbolInfo, box: string): void {
    const ptr = this.alloca();
    this.emit(`  store i64 ${box}, i64* ${ptr}`);
    // A captured symbol is always boxed; the environment holds the box.
    this.current.slots.set(symbol.id, { ptr, boxed: true });
  }

  private readSlot(symbol: SymbolInfo): string {
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

  private writeSlot(symbol: SymbolInfo, value: string): void {
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

  private emitDefaultParameter(symbol: SymbolInfo, value: string, initializer: Expression): void {
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
  }

  // -- statements ----------------------------------------------------------

  private emitStatements(statements: readonly Statement[]): void {
    for (const statement of statements) {
      if (this.current.terminated) break;
      this.emitStatement(statement);
    }
  }

  private emitStatement(statement: Statement): void {
    switch (statement.kind) {
      case SyntaxKind.ExpressionStatement:
        this.emitExpression((statement as { expression: Expression }).expression);
        return;
      case SyntaxKind.VariableStatement:
        this.emitVariableStatement(statement as VariableStatement);
        return;
      case SyntaxKind.Block:
        this.emitStatements((statement as Block).statements);
        return;
      case SyntaxKind.EmptyStatement:
      case SyntaxKind.DebuggerStatement:
      case SyntaxKind.FunctionDeclaration:
      case SyntaxKind.InterfaceDeclaration:
      case SyntaxKind.TypeAliasDeclaration:
        return; // functions are emitted at module scope
      case SyntaxKind.IfStatement:
        this.emitIf(statement as IfStatement);
        return;
      case SyntaxKind.WhileStatement:
        this.emitWhile(statement as WhileStatement);
        return;
      case SyntaxKind.DoStatement:
        this.emitDo(statement as DoStatement);
        return;
      case SyntaxKind.ForStatement:
        this.emitFor(statement as ForStatement);
        return;
      case SyntaxKind.ForOfStatement:
      case SyntaxKind.ForInStatement:
        this.emitForOf(statement as ForOfStatement | ForInStatement);
        return;
      case SyntaxKind.SwitchStatement:
        this.emitSwitch(statement as SwitchStatement);
        return;
      case SyntaxKind.TryStatement:
        this.emitTry(statement as TryStatement);
        return;
      case SyntaxKind.ReturnStatement:
        this.emitReturn(statement as ReturnStatement);
        return;
      case SyntaxKind.BreakStatement: {
        const loop = this.current.loops[this.current.loops.length - 1];
        if (loop) {
          this.popTryFramesTo(loop.tryDepth ?? 0);
          this.terminate(`br label %${loop.breakLabel}`);
        }
        return;
      }
      case SyntaxKind.ContinueStatement: {
        const loop = this.current.loops[this.current.loops.length - 1];
        if (loop) {
          this.popTryFramesTo(loop.tryDepth ?? 0);
          this.terminate(`br label %${loop.continueLabel}`);
        }
        return;
      }
      case SyntaxKind.ThrowStatement: {
        const value = this.emitExpression((statement as ThrowStatement).expression);
        this.emit(`  call void @xt_throw(i64 ${value})`);
        this.terminate("unreachable");
        return;
      }
      case SyntaxKind.ExportDeclaration: {
        const declaration = (statement as { declaration?: Statement }).declaration;
        if (declaration) this.emitStatement(declaration);
        return;
      }
      default:
        this.unsupported(statement, "statement");
    }
  }

  private emitVariableStatement(statement: VariableStatement): void {
    for (const declaration of statement.declarationList.declarations) {
      const symbol = this.binding.symbolOfDeclaration.get(declaration);
      if (!symbol) continue;
      if (this.current.slots.has(symbol.id)) continue; // hoisted `var`
      const initial = declaration.initializer
        ? this.emitExpression(declaration.initializer)
        : i64(XT_UNDEFINED);
      this.declareSlot(symbol, initial);
    }
  }

  private emitIf(statement: IfStatement): void {
    const thenLabel = this.label("if.then");
    const elseLabel = statement.elseStatement ? this.label("if.else") : undefined;
    const endLabel = this.label("if.end");
    const condition = this.emitExpression(statement.condition);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    this.terminate(`br i1 ${nonzero}, label %${thenLabel}, label %${elseLabel ?? endLabel}`);

    this.startBlock(thenLabel);
    this.emitStatement(statement.thenStatement);
    if (!this.current.terminated) this.terminate(`br label %${endLabel}`);

    if (elseLabel) {
      this.startBlock(elseLabel);
      this.emitStatement(statement.elseStatement!);
      if (!this.current.terminated) this.terminate(`br label %${endLabel}`);
    }

    this.startBlock(endLabel);
  }

  private emitWhile(statement: WhileStatement): void {
    const condLabel = this.label("while.cond");
    const bodyLabel = this.label("while.body");
    const endLabel = this.label("while.end");
    this.terminate(`br label %${condLabel}`);
    this.startBlock(condLabel);
    const condition = this.emitExpression(statement.condition);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    this.terminate(`br i1 ${nonzero}, label %${bodyLabel}, label %${endLabel}`);

    this.startBlock(bodyLabel);
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel, tryDepth: this.current.tryFrames.length });
    this.emitStatement(statement.statement);
    this.current.loops.pop();
    if (!this.current.terminated) this.terminate(`br label %${condLabel}`);
    this.startBlock(endLabel);
  }

  private emitDo(statement: DoStatement): void {
    const bodyLabel = this.label("do.body");
    const condLabel = this.label("do.cond");
    const endLabel = this.label("do.end");
    this.terminate(`br label %${bodyLabel}`);
    this.startBlock(bodyLabel);
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel, tryDepth: this.current.tryFrames.length });
    this.emitStatement(statement.statement);
    this.current.loops.pop();
    if (!this.current.terminated) this.terminate(`br label %${condLabel}`);

    this.startBlock(condLabel);
    const condition = this.emitExpression(statement.condition);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    this.terminate(`br i1 ${nonzero}, label %${bodyLabel}, label %${endLabel}`);
    this.startBlock(endLabel);
  }

  private emitFor(statement: ForStatement): void {
    if (statement.initializer && statement.initializer.kind === SyntaxKind.VariableDeclarationList) {
      this.emitVariableStatement({
        kind: SyntaxKind.VariableStatement,
        declarationList: statement.initializer as VariableDeclarationList,
        modifiers: [],
        start: statement.initializer.start,
        end: statement.initializer.end,
      });
    } else if (statement.initializer) {
      this.emitExpression(statement.initializer as Expression);
    }
    const condLabel = this.label("for.cond");
    const bodyLabel = this.label("for.body");
    const updateLabel = this.label("for.update");
    const endLabel = this.label("for.end");
    this.terminate(`br label %${condLabel}`);

    this.startBlock(condLabel);
    if (statement.condition) {
      const condition = this.emitExpression(statement.condition);
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
      const nonzero = this.reg();
      this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
      this.terminate(`br i1 ${nonzero}, label %${bodyLabel}, label %${endLabel}`);
    } else {
      this.terminate(`br label %${bodyLabel}`);
    }

    this.startBlock(bodyLabel);
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel, tryDepth: this.current.tryFrames.length });
    this.emitStatement(statement.statement);
    this.current.loops.pop();
    if (!this.current.terminated) this.terminate(`br label %${updateLabel}`);

    this.startBlock(updateLabel);
    if (statement.incrementor) this.emitExpression(statement.incrementor);
    this.terminate(`br label %${condLabel}`);
    this.startBlock(endLabel);
  }

  /** `for (const x of xs)` / `for (const k in obj)` over arrays, strings and objects. */
  private emitForOf(statement: ForOfStatement | ForInStatement): void {
    const source = this.emitExpression(statement.expression);
    const isForIn = statement.kind === SyntaxKind.ForInStatement;
    // `for...in` iterates the enumerable keys (indices become strings);
    // `for...of` iterates the values at each index.
    const iterable = isForIn ? this.runtimeCall("xt_object_keys", [source]) : source;
    const indexPtr = this.alloca();
    this.emit(`  store i64 ${numberLiteral(0)}, i64* ${indexPtr}`);
    const lengthValue = this.reg();
    this.emit(`  ${lengthValue} = call i64 @xt_array_length(i64 ${iterable})`);

    const condLabel = this.label("forof.cond");
    const bodyLabel = this.label("forof.body");
    const updateLabel = this.label("forof.update");
    const endLabel = this.label("forof.end");
    this.terminate(`br label %${condLabel}`);

    this.startBlock(condLabel);
    const index = this.reg();
    this.emit(`  ${index} = load i64, i64* ${indexPtr}`);
    const inRange = this.reg();
    this.emit(`  ${inRange} = call i64 @xt_lt(i64 ${index}, i64 ${lengthValue})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${inRange})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    this.terminate(`br i1 ${nonzero}, label %${bodyLabel}, label %${endLabel}`);

    this.startBlock(bodyLabel);
    const element = this.reg();
    this.emit(`  ${element} = call i64 @xt_get(i64 ${iterable}, i64 ${index})`);
    this.bindLoopVariable(statement.initializer, element);
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel, tryDepth: this.current.tryFrames.length });
    this.emitStatement(statement.statement);
    this.current.loops.pop();
    if (!this.current.terminated) this.terminate(`br label %${updateLabel}`);

    this.startBlock(updateLabel);
    const next = this.reg();
    this.emit(`  ${next} = call i64 @xt_add(i64 ${index}, i64 ${numberLiteral(1)})`);
    this.emit(`  store i64 ${next}, i64* ${indexPtr}`);
    this.terminate(`br label %${condLabel}`);
    this.startBlock(endLabel);
  }

  private bindLoopVariable(initializer: VariableDeclarationList | Expression, value: string): void {
    if (initializer.kind === SyntaxKind.VariableDeclarationList) {
      const declaration = initializer.declarations[0];
      if (!declaration) return;
      const symbol = this.binding.symbolOfDeclaration.get(declaration);
      if (symbol) {
        if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
        else this.declareSlot(symbol, value);
      }
      return;
    }
    this.emitAssignmentTarget(initializer as Expression, value);
  }

  /**
   * JavaScript `switch`: test each `case` with strict equality, then run the
   * matched clause and fall through into the following clauses until `break`.
   */
  private emitSwitch(statement: SwitchStatement): void {
    const discriminant = this.emitExpression(statement.expression);
    const endLabel = this.label("switch.end");
    const clauses = statement.clauses;
    const labels = clauses.map(() => this.label("switch.case"));
    const testLabels = clauses.map((clause) =>
      clause.kind === SyntaxKind.CaseClause ? this.label("switch.test") : undefined,
    );
    const defaultIndex = clauses.findIndex((clause) => clause.kind === SyntaxKind.DefaultClause);
    const defaultLabel = defaultIndex >= 0 ? labels[defaultIndex]! : endLabel;
    const caseIndexes = clauses
      .map((clause, index) => ({ clause, index }))
      .filter((entry) => entry.clause.kind === SyntaxKind.CaseClause);

    this.terminate(`br label %${caseIndexes.length > 0 ? testLabels[caseIndexes[0]!.index]! : defaultLabel}`);
    for (let test = 0; test < caseIndexes.length; test++) {
      const entry = caseIndexes[test]!;
      this.startBlock(testLabels[entry.index]!);
      const caseValue = this.emitExpression((entry.clause as CaseClause).expression);
      const equals = this.reg();
      this.emit(`  ${equals} = call i64 @xt_seq(i64 ${discriminant}, i64 ${caseValue})`);
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${equals})`);
      const condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
      const next = test + 1 < caseIndexes.length ? testLabels[caseIndexes[test + 1]!.index]! : defaultLabel;
      this.terminate(`br i1 ${condition}, label %${labels[entry.index]}, label %${next}`);
    }

    for (let index = 0; index < clauses.length; index++) {
      this.startBlock(labels[index]!);
      const enclosing = this.current.loops[this.current.loops.length - 1];
      this.current.loops.push({ breakLabel: endLabel, continueLabel: enclosing?.continueLabel ?? endLabel, tryDepth: this.current.tryFrames.length });
      for (const child of clauses[index]!.statements) {
        if (this.current.terminated) break;
        this.emitStatement(child);
      }
      this.current.loops.pop();
      if (!this.current.terminated) {
        const next = index + 1 < clauses.length ? labels[index + 1]! : endLabel;
        this.terminate(`br label %${next}`);
      }
    }
    this.startBlock(endLabel);
  }

  /**
   * `try`/`catch`/`finally` via a runtime setjmp frame. The runtime keeps a
   * stack of frames; `xt_throw` longjmps into the innermost one. A `return`
   * that exits the protected region skips `finally` (a known limitation).
   */
  private emitTry(statement: TryStatement): void {
    this.current.usesTry = true;
    const frameSlot = this.alloca();
    const exceptionSlot = this.alloca();
    const flagSlot = this.alloca();
    const tryLabel = this.label("try.body");
    const catchLabel = this.label("try.catch");
    const exceptionLabel = this.label("try.exception");
    const rethrowLabel = this.label("try.rethrow");
    const finallyLabel = statement.finallyBlock ? this.label("try.finally") : undefined;
    const endLabel = this.label("try.end");
    const hasCatch = !!statement.catchClause;

    const frame = this.reg();
    this.emit(`  ${frame} = call i8* @xt_try_enter()`);
    this.emit(`  store i8* ${frame}, i8** ${frameSlot}`);
    this.emit(`  store i64 0, i64* ${flagSlot}`);
    const jump = this.reg();
    this.emit(`  ${jump} = call i32 @_setjmp(i8* ${frame})`);
    const isThrow = this.reg();
    this.emit(`  ${isThrow} = icmp ne i32 ${jump}, 0`);
    const exceptionTarget = hasCatch ? catchLabel : exceptionLabel;
    this.terminate(`br i1 ${isThrow}, label %${exceptionTarget}, label %${tryLabel}`);

    // Normal completion of the try block.
    this.startBlock(tryLabel);
    this.current.tryFrames.push(frameSlot);
    this.emitStatements(statement.tryBlock.statements);
    this.current.tryFrames.pop();
    if (!this.current.terminated) {
      const currentFrame = this.reg();
      this.emit(`  ${currentFrame} = load i8*, i8** ${frameSlot}`);
      this.emit(`  call void @xt_try_leave(i8* ${currentFrame})`);
      this.terminate(`br label %${finallyLabel ?? endLabel}`);
    }

    if (hasCatch) {
      this.startBlock(catchLabel);
      const currentFrame = this.reg();
      this.emit(`  ${currentFrame} = load i8*, i8** ${frameSlot}`);
      const exception = this.reg();
      this.emit(`  ${exception} = call i64 @xt_try_exception(i8* ${currentFrame})`);
      this.emit(`  call void @xt_try_leave(i8* ${currentFrame})`);
      this.bindCatchVariable(statement.catchClause!.variable, exception);
      this.emitStatements(statement.catchClause!.block.statements);
      if (!this.current.terminated) this.terminate(`br label %${finallyLabel ?? endLabel}`);
    } else {
      // Without a catch clause the exception is remembered, then rethrown
      // after `finally` runs.
      this.startBlock(exceptionLabel);
      const currentFrame = this.reg();
      this.emit(`  ${currentFrame} = load i8*, i8** ${frameSlot}`);
      const exception = this.reg();
      this.emit(`  ${exception} = call i64 @xt_try_exception(i8* ${currentFrame})`);
      this.emit(`  call void @xt_try_leave(i8* ${currentFrame})`);
      this.emit(`  store i64 ${exception}, i64* ${exceptionSlot}`);
      this.emit(`  store i64 1, i64* ${flagSlot}`);
      this.terminate(`br label %${finallyLabel ?? rethrowLabel}`);
    }

    if (finallyLabel) {
      this.startBlock(finallyLabel);
      this.emitStatements(statement.finallyBlock!.statements);
      if (!this.current.terminated) {
        if (hasCatch) {
          this.terminate(`br label %${endLabel}`);
        } else {
          const flag = this.reg();
          this.emit(`  ${flag} = load i64, i64* ${flagSlot}`);
          const truthy = this.reg();
          this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${flag})`);
          const shouldRethrow = this.reg();
          this.emit(`  ${shouldRethrow} = icmp ne i32 ${truthy}, 0`);
          this.terminate(`br i1 ${shouldRethrow}, label %${rethrowLabel}, label %${endLabel}`);
        }
      }
    }

    if (!hasCatch) {
      this.startBlock(rethrowLabel);
      const exception = this.reg();
      this.emit(`  ${exception} = load i64, i64* ${exceptionSlot}`);
      this.emit(`  call void @xt_throw(i64 ${exception})`);
      this.terminate("unreachable");
    }

    this.startBlock(endLabel);
  }

  private bindCatchVariable(variable: Identifier | undefined, value: string): void {
    if (!variable) return;
    const symbol = this.binding.symbolOfDeclaration.get(variable);
    if (!symbol) return;
    if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
    else this.declareSlot(symbol, value);
  }

  private emitReturn(statement: ReturnStatement): void {
    const value = statement.expression ? this.emitExpression(statement.expression) : i64(XT_UNDEFINED);
    this.popTryFramesTo(0);
    this.terminate(`ret i64 ${value}`);
  }

  /** Emit `xt_try_leave` for every active frame above `depth` (innermost first). */
  private popTryFramesTo(depth: number): void {
    for (let index = this.current.tryFrames.length - 1; index >= depth; index--) {
      const slot = this.current.tryFrames[index]!;
      const frame = this.reg();
      this.emit(`  ${frame} = load i8*, i8** ${slot}`);
      this.emit(`  call void @xt_try_leave(i8* ${frame})`);
    }
  }

  // -- expressions ---------------------------------------------------------

  private emitExpression(node: Expression): string {
    switch (node.kind) {
      case SyntaxKind.Identifier:
        return this.emitIdentifier(node as Identifier);
      case SyntaxKind.NumericLiteral: {
        const value = (node as { value: number }).value;
        return numberLiteral(value);
      }
      case SyntaxKind.BigIntLiteral:
        return numberLiteral(Number((node as { value: bigint }).value));
      case SyntaxKind.StringLiteral:
        return this.stringValue((node as { value: string }).value);
      case SyntaxKind.NoSubstitutionTemplateLiteral:
        return this.stringValue((node as { value: string }).value);
      case SyntaxKind.TemplateLiteral:
        return this.emitTemplate(node as TemplateLiteral);
      case SyntaxKind.TrueKeyword:
        return i64(XT_TRUE);
      case SyntaxKind.FalseKeyword:
        return i64(XT_FALSE);
      case SyntaxKind.NullKeyword:
        return i64(XT_NULL);
      case SyntaxKind.UndefinedKeyword:
        return i64(XT_UNDEFINED);
      case SyntaxKind.ParenthesizedExpression:
        return this.emitExpression((node as { expression: Expression }).expression);
      case SyntaxKind.AsExpression:
      case SyntaxKind.SatisfiesExpression:
      case SyntaxKind.NonNullExpression:
        return this.emitExpression((node as { expression: Expression }).expression);
      case SyntaxKind.DeleteExpression:
        return this.emitDelete(node as DeleteExpression);
      case SyntaxKind.BinaryExpression:
        return this.emitBinaryOrAssignment(node as BinaryExpression);
      case SyntaxKind.PrefixUnaryExpression:
        return this.emitPrefix(node as PrefixUnaryExpression);
      case SyntaxKind.PostfixUnaryExpression:
        return this.emitPostfix(node as PostfixUnaryExpression);
      case SyntaxKind.ConditionalExpression:
        return this.emitConditional(node as ConditionalExpression);
      case SyntaxKind.CallExpression:
        return this.emitCall(node as CallExpression);
      case SyntaxKind.PropertyAccessExpression:
        return this.emitPropertyAccess(node as PropertyAccessExpression);
      case SyntaxKind.ElementAccessExpression:
        return this.emitElementAccess(node as ElementAccessExpression);
      case SyntaxKind.ArrayLiteralExpression:
        return this.emitArrayLiteral(node as ArrayLiteralExpression);
      case SyntaxKind.ObjectLiteralExpression:
        return this.emitObjectLiteral(node as ObjectLiteralExpression);
      case SyntaxKind.ArrowFunction:
      case SyntaxKind.FunctionExpression:
        return this.emitClosure(node as ArrowFunction | FunctionExpression);
      default:
        this.unsupported(node, "expression");
        return i64(XT_UNDEFINED);
    }
  }

  private emitIdentifier(identifier: Identifier): string {
    const symbol = this.binding.symbolOfIdentifier.get(identifier);
    if (symbol) {
      if (symbol.kind === SymbolKind.Function && !this.current.slots.has(symbol.id)) {
        return this.emitFunctionValue(symbol);
      }
      return this.readSlot(symbol);
    }
    switch (identifier.text) {
      case "undefined":
        return i64(XT_UNDEFINED);
      case "NaN":
        return numberLiteral(NaN);
      case "Infinity":
        return numberLiteral(Infinity);
      case "console":
        return i64(XT_UNDEFINED);
      case "arguments": {
        const argc = this.reg();
        this.emit(`  ${argc} = load i32, i32* %saved.argc`);
        const argv = this.reg();
        this.emit(`  ${argv} = load i64*, i64** %saved.argv`);
        const rest = this.reg();
        this.emit(`  ${rest} = call i64 @xt_rest_args(i32 ${argc}, i64* ${argv}, i32 0)`);
        return rest;
      }
      default:
        this.diagnostics.error(
          DiagnosticCode.CannotFindName,
          `Cannot find name '${identifier.text}'`,
          identifier,
          this.sourceFile.fileName,
        );
        return i64(XT_UNDEFINED);
    }
  }

  private emitFunctionValue(symbol: SymbolInfo): string {
    const declaration = symbol.declarations[0];
    const fn = declaration ? this.binding.functionOfNode.get(declaration) : undefined;
    if (!fn) return i64(XT_UNDEFINED);
    const cast = this.reg();
    this.emit(`  ${cast} = bitcast i64 (i64, i32, i64*)* @${this.functionName(fn)} to i8*`);
    const closure = this.reg();
    this.emit(`  ${closure} = call i64 @xt_closure_new(i8* ${cast}, i32 0, i64* null)`);
    return closure;
  }

  private emitTemplate(node: TemplateLiteral): string {
    let accumulator = this.stringValue(node.head);
    for (const span of node.spans) {
      const expression = this.emitExpression(span.expression);
      const text = this.reg();
      this.emit(`  ${text} = call i64 @xt_to_string(i64 ${expression})`);
      const joined = this.reg();
      this.emit(`  ${joined} = call i64 @xt_add(i64 ${accumulator}, i64 ${text})`);
      accumulator = joined;
      if (span.literal.length > 0) {
        const literal = this.stringValue(span.literal);
        const withLiteral = this.reg();
        this.emit(`  ${withLiteral} = call i64 @xt_add(i64 ${accumulator}, i64 ${literal})`);
        accumulator = withLiteral;
      }
    }
    return accumulator;
  }

  private emitBinaryOrAssignment(node: BinaryExpression): string {
    const operator = node.operator as string;
    if (isAssignmentOperator(operator)) {
      return this.emitAssignment(node);
    }
    switch (operator) {
      case BinaryOperator.AmpersandAmpersand:
        return this.emitShortCircuit(node, "and");
      case BinaryOperator.BarBar:
        return this.emitShortCircuit(node, "or");
      case BinaryOperator.QuestionQuestion:
        return this.emitShortCircuit(node, "nullish");
      case BinaryOperator.Comma: {
        this.emitExpression(node.left);
        return this.emitExpression(node.right);
      }
      default:
        break;
    }
    const left = this.emitExpression(node.left);
    const right = this.emitExpression(node.right);
    const runtime = BINARY_RUNTIME[operator];
    if (!runtime) {
      this.unsupported(node, `operator '${operator}'`);
      return i64(XT_UNDEFINED);
    }
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${runtime}(i64 ${left}, i64 ${right})`);
    return result;
  }

  private emitShortCircuit(node: BinaryExpression, kind: "and" | "or" | "nullish"): string {
    const result = this.alloca();
    const left = this.emitExpression(node.left);
    this.emit(`  store i64 ${left}, i64* ${result}`);
    const rightLabel = this.label("sc.right");
    const endLabel = this.label("sc.end");
    let condition: string;
    if (kind === "and") {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${left})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    } else if (kind === "or") {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${left})`);
      const isTruthy = this.reg();
      this.emit(`  ${isTruthy} = icmp ne i32 ${truthy}, 0`);
      condition = this.reg();
      this.emit(`  ${condition} = xor i1 ${isTruthy}, true`);
    } else {
      const nullish = this.reg();
      this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${left})`);
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${nullish})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    }
    this.terminate(`br i1 ${condition}, label %${rightLabel}, label %${endLabel}`);
    this.startBlock(rightLabel);
    const right = this.emitExpression(node.right);
    this.emit(`  store i64 ${right}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    return value;
  }

  private emitConditional(node: ConditionalExpression): string {
    const result = this.alloca();
    const condition = this.emitExpression(node.condition);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    const trueLabel = this.label("cond.true");
    const falseLabel = this.label("cond.false");
    const endLabel = this.label("cond.end");
    this.terminate(`br i1 ${nonzero}, label %${trueLabel}, label %${falseLabel}`);
    this.startBlock(trueLabel);
    const whenTrue = this.emitExpression(node.whenTrue);
    this.emit(`  store i64 ${whenTrue}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(falseLabel);
    const whenFalse = this.emitExpression(node.whenFalse);
    this.emit(`  store i64 ${whenFalse}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    return value;
  }

  private emitPrefix(node: PrefixUnaryExpression): string {
    switch (node.operator) {
      case PrefixUnaryOperator.Minus: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_neg(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Plus: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_pos(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Exclamation: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_not(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Tilde: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_bit_not(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.TypeOf: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_typeof(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Void:
        if (node.operand) this.emitExpression(node.operand);
        return i64(XT_UNDEFINED);
      case PrefixUnaryOperator.PlusPlus:
      case PrefixUnaryOperator.MinusMinus: {
        const one = numberLiteral(1);
        const current = this.emitExpression(node.operand);
        const fn = node.operator === PrefixUnaryOperator.PlusPlus ? "xt_add" : "xt_sub";
        const next = this.reg();
        this.emit(`  ${next} = call i64 @${fn}(i64 ${current}, i64 ${one})`);
        this.emitAssignmentTarget(node.operand, next);
        return next;
      }
      default:
        this.unsupported(node, `unary operator '${node.operator}'`);
        return i64(XT_UNDEFINED);
    }
  }

  private emitPostfix(node: PostfixUnaryExpression): string {
    const one = numberLiteral(1);
    const current = this.emitExpression(node.operand);
    const fn = node.operator === PostfixUnaryOperator.PlusPlus ? "xt_add" : "xt_sub";
    const next = this.reg();
    this.emit(`  ${next} = call i64 @${fn}(i64 ${current}, i64 ${one})`);
    this.emitAssignmentTarget(node.operand, next);
    return current;
  }

  private emitAssignment(node: BinaryExpression): string {
    const operator = node.operator as unknown as AssignmentOperator;
    const target = node.left;
    let value: string;
    if (operator === AssignmentOperator.Assign) {
      value = this.emitExpression(node.right);
    } else if (
      operator === AssignmentOperator.AmpersandAmpersandAssign ||
      operator === AssignmentOperator.BarBarAssign ||
      operator === AssignmentOperator.QuestionQuestionAssign
    ) {
      value = this.emitLogicalAssignment(node);
    } else {
      const current = this.emitExpression(target);
      const right = this.emitExpression(node.right);
      const binaryOperator = compoundToBinary(operator);
      const runtime = BINARY_RUNTIME[binaryOperator];
      if (!runtime) {
        this.unsupported(node, `assignment operator '${operator}'`);
        return i64(XT_UNDEFINED);
      }
      const result = this.reg();
      this.emit(`  ${result} = call i64 @${runtime}(i64 ${current}, i64 ${right})`);
      value = result;
    }
    this.emitAssignmentTarget(target, value);
    return value;
  }

  private emitLogicalAssignment(node: BinaryExpression): string {
    const operator = node.operator as unknown as AssignmentOperator;
    const target = node.left;
    const current = this.emitExpression(target);
    const result = this.alloca();
    this.emit(`  store i64 ${current}, i64* ${result}`);
    const rightLabel = this.label("assign.right");
    const endLabel = this.label("assign.end");
    let condition: string;
    if (operator === AssignmentOperator.AmpersandAmpersandAssign) {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${current})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    } else if (operator === AssignmentOperator.BarBarAssign) {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${current})`);
      const isTruthy = this.reg();
      this.emit(`  ${isTruthy} = icmp ne i32 ${truthy}, 0`);
      condition = this.reg();
      this.emit(`  ${condition} = xor i1 ${isTruthy}, true`);
    } else {
      const nullish = this.reg();
      this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${current})`);
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${nullish})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    }
    this.terminate(`br i1 ${condition}, label %${rightLabel}, label %${endLabel}`);
    this.startBlock(rightLabel);
    const right = this.emitExpression(node.right);
    this.emit(`  store i64 ${right}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    this.emitAssignmentTarget(target, value);
    return value;
  }

  private emitAssignmentTarget(target: Expression, value: string): void {
    switch (target.kind) {
      case SyntaxKind.Identifier: {
        const symbol = this.binding.symbolOfIdentifier.get(target as Identifier);
        if (symbol) this.writeSlot(symbol, value);
        return;
      }
      case SyntaxKind.PropertyAccessExpression: {
        const access = target as PropertyAccessExpression;
        const object = this.emitExpression(access.expression);
        const key = this.stringValue(access.name.text);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
        return;
      }
      case SyntaxKind.ElementAccessExpression: {
        const access = target as ElementAccessExpression;
        const object = this.emitExpression(access.expression);
        const key = this.emitExpression(access.argumentExpression);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
        return;
      }
      case SyntaxKind.ParenthesizedExpression:
        this.emitAssignmentTarget((target as { expression: Expression }).expression, value);
        return;
      default:
        this.unsupported(target, "assignment target");
    }
  }

  private emitCall(node: CallExpression): string {
    const callee = node.expression;
    if (node.optional) {
      const calleeValue = this.emitExpression(callee);
      return this.emitOptional(calleeValue, () => {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_closure_call(i64 ${calleeValue}, i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      });
    }
    if (
      (callee.kind === SyntaxKind.PropertyAccessExpression || callee.kind === SyntaxKind.ElementAccessExpression) &&
      (callee as PropertyAccessExpression | ElementAccessExpression).optional
    ) {
      // `obj?.method(args)` / `obj?.[key](args)`: guard the receiver, then
      // dispatch through the runtime, which understands object methods as well
      // as the built-in array/string methods.
      const access = callee as PropertyAccessExpression | ElementAccessExpression;
      const object = this.emitExpression(access.expression);
      return this.emitOptional(object, () => {
        const name =
          access.kind === SyntaxKind.PropertyAccessExpression
            ? this.stringValue((access as PropertyAccessExpression).name.text)
            : this.emitExpression((access as ElementAccessExpression).argumentExpression);
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(
          `  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`,
        );
        return result;
      });
    }
    if (callee.kind === SyntaxKind.PropertyAccessExpression) {
      const special = this.tryEmitBuiltinCall(node, callee as PropertyAccessExpression);
      if (special) return special;
    }
    if (callee.kind === SyntaxKind.Identifier) {
      const symbol = this.binding.symbolOfIdentifier.get(callee as Identifier);
      if (symbol && symbol.kind === SymbolKind.Function) {
        const declaration = symbol.declarations[0];
        const fn = declaration ? this.binding.functionOfNode.get(declaration) : undefined;
        if (fn) {
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(
            `  ${result} = call i64 @${this.functionName(fn)}(i64 ${i64(XT_UNDEFINED)}, i32 ${args.argc}, i64* ${args.ptr})`,
          );
          return result;
        }
      }
      const globalFunction = !symbol ? GLOBAL_FUNCTIONS[(callee as Identifier).text] : undefined;
      if (globalFunction) {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${globalFunction}(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
      const builtin = this.builtins[(callee as Identifier).text];
      if (!symbol && builtin) {
        this.extraDeclarations.add(
          builtin.returnVoid ? `declare void @${builtin.symbol}(i32, i64*)` : `declare i64 @${builtin.symbol}(i32, i64*)`,
        );
        const args = this.emitArguments(node.arguments);
        if (builtin.returnVoid) {
          this.emit(`  call void @${builtin.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
          return i64(XT_UNDEFINED);
        }
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${builtin.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
    }
    const calleeValue = this.emitExpression(callee);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_closure_call(i64 ${calleeValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  }

  private tryEmitBuiltinCall(node: CallExpression, callee: PropertyAccessExpression): string | undefined {
    const target = callee.expression;
    const method = callee.name.text;
    const targetIdentifier = target.kind === SyntaxKind.Identifier ? (target as Identifier) : undefined;
    const targetSymbol = targetIdentifier ? this.binding.symbolOfIdentifier.get(targetIdentifier) : undefined;

    if (targetIdentifier && targetIdentifier.text === "console" && !targetSymbol) {
      const consoleFn = CONSOLE_METHODS[method];
      if (consoleFn) {
        const args = this.emitArguments(node.arguments);
        this.emit(`  call void @${consoleFn}(i32 ${args.argc}, i64* ${args.ptr})`);
        return i64(XT_UNDEFINED);
      }
      return undefined;
    }

    if (targetIdentifier && targetIdentifier.text === "Math" && !targetSymbol) {
      if (!MATH_FUNCTIONS.has(method)) return undefined;
      const name = this.stringValue(method);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_math_call(i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }

    if (targetIdentifier && targetIdentifier.text === "Object" && !targetSymbol) {
      if (method === "keys" || method === "values" || method === "entries") {
        const argument = node.arguments.length > 0 ? this.emitExpression(node.arguments[0]!) : i64(XT_UNDEFINED);
        const fn = method === "keys" ? "xt_object_keys" : method === "values" ? "xt_object_values" : "xt_object_entries";
        return this.runtimeCall(fn, [argument]);
      }
      if (method === "assign") {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_object_assign(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
      return undefined;
    }

    if (BUILTIN_METHODS.has(method)) {
      const object = this.emitExpression(target);
      const name = this.stringValue(method);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(
        `  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`,
      );
      return result;
    }
    return undefined;
  }

  private runtimeCall(name: string, args: readonly string[]): string {
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${name}(${args.map((a) => `i64 ${a}`).join(", ")})`);
    return result;
  }

  private emitArguments(args: readonly Expression[]): { argc: number; ptr: string } {
    if (args.length === 0) return { argc: 0, ptr: "null" };
    const ptr = `%args${this.current.allocas.length}`;
    this.current.allocas.push(`${ptr} = alloca i64, i32 ${args.length}`);
    for (let index = 0; index < args.length; index++) {
      const value = this.emitExpression(args[index]!);
      const slot = this.reg();
      this.emit(`  ${slot} = getelementptr i64, i64* ${ptr}, i32 ${index}`);
      this.emit(`  store i64 ${value}, i64* ${slot}`);
    }
    return { argc: args.length, ptr };
  }

  private emitDelete(node: DeleteExpression): string {
    const target = node.expression;
    if (target.kind === SyntaxKind.PropertyAccessExpression) {
      const access = target as PropertyAccessExpression;
      const object = this.emitExpression(access.expression);
      const key = this.stringValue(access.name.text);
      return this.runtimeCall("xt_delete", [object, key]);
    }
    if (target.kind === SyntaxKind.ElementAccessExpression) {
      const access = target as ElementAccessExpression;
      const object = this.emitExpression(access.expression);
      const key = this.emitExpression(access.argumentExpression);
      return this.runtimeCall("xt_delete", [object, key]);
    }
    return i64(XT_TRUE);
  }

  private emitPropertyAccess(node: PropertyAccessExpression): string {
    if (
      node.name.text in MATH_CONSTANTS &&
      node.expression.kind === SyntaxKind.Identifier &&
      (node.expression as Identifier).text === "Math" &&
      !this.binding.symbolOfIdentifier.get(node.expression as Identifier)
    ) {
      return numberLiteral(MATH_CONSTANTS[node.name.text]!);
    }
    const object = this.emitExpression(node.expression);
    const access = (): string => {
      if (node.name.text === "length") {
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_array_length(i64 ${object})`);
        return result;
      }
      const key = this.stringValue(node.name.text);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
      return result;
    };
    return node.optional ? this.emitOptional(object, access) : access();
  }

  private emitElementAccess(node: ElementAccessExpression): string {
    const object = this.emitExpression(node.expression);
    const access = (): string => {
      const key = this.emitExpression(node.argumentExpression);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
      return result;
    };
    return node.optional ? this.emitOptional(object, access) : access();
  }

  /**
   * Evaluate `object` once and, when it is neither `null` nor `undefined`, run
   * `compute` to produce the value; otherwise short-circuit to `undefined`.
   */
  private emitOptional(objectValue: string, compute: () => string): string {
    const result = this.alloca();
    this.emit(`  store i64 ${i64(XT_UNDEFINED)}, i64* ${result}`);
    const nullish = this.reg();
    this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${objectValue})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${nullish})`);
    const condition = this.reg();
    this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    const someLabel = this.label("opt.some");
    const endLabel = this.label("opt.end");
    this.terminate(`br i1 ${condition}, label %${endLabel}, label %${someLabel}`);
    this.startBlock(someLabel);
    const value = compute();
    this.emit(`  store i64 ${value}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const merged = this.reg();
    this.emit(`  ${merged} = load i64, i64* ${result}`);
    return merged;
  }

  private emitArrayLiteral(node: ArrayLiteralExpression): string {
    const hasSpread = node.elements.some((element) => element.kind === SyntaxKind.SpreadElement);
    if (!hasSpread) {
      const args = this.emitArguments(node.elements);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_array_new(i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    let array = this.runtimeCall("xt_array_new", [`0`, `null`]);
    for (const element of node.elements) {
      if (element.kind === SyntaxKind.SpreadElement) {
        const spread = this.emitExpression((element as { expression: Expression }).expression);
        array = this.runtimeCall("xt_array_spread", [array, spread]);
      } else {
        const value = this.emitExpression(element);
        array = this.runtimeCall("xt_array_push", [array, value]);
      }
    }
    return array;
  }

  private emitObjectLiteral(node: ObjectLiteralExpression): string {
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    for (const property of node.properties) {
      if (property.kind === SyntaxKind.PropertyAssignment) {
        const name = propertyNameText(property.name);
        const key = this.stringValue(name);
        const value = this.emitExpression(property.initializer);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      } else if (property.kind === SyntaxKind.ShorthandPropertyAssignment) {
        const identifier = property.name;
        const key = this.stringValue(identifier.text);
        const value = this.emitIdentifier(identifier);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      } else if (property.kind === SyntaxKind.SpreadElement) {
        const spread = this.emitExpression((property as { expression: Expression }).expression);
        this.emit(`  call i64 @xt_object_spread(i64 ${object}, i64 ${spread})`);
      } else {
        this.unsupported(property, "object spread");
      }
    }
    return object;
  }

  private emitClosure(node: ArrowFunction | FunctionExpression): string {
    const fn = this.binding.functionOfNode.get(node);
    if (!fn) {
      this.unsupported(node, "closure");
      return i64(XT_UNDEFINED);
    }
    let envPtr = "null";
    if (fn.captures.length > 0) {
      envPtr = `%env${this.current.allocas.length}`;
      this.current.allocas.push(`${envPtr} = alloca i64, i32 ${fn.captures.length}`);
      for (let index = 0; index < fn.captures.length; index++) {
        const symbol = fn.captures[index]!;
        // The current function must be able to read the capture: either from
        // its own slot (local) or from one of its own capture slots.
        const slot = this.current.slots.get(symbol.id);
        let raw: string;
        if (slot) {
          raw = this.reg();
          this.emit(`  ${raw} = load i64, i64* ${slot.ptr}`);
        } else {
          const outer = this.binding.functionOfNode.get(node);
          void outer;
          raw = i64(XT_UNDEFINED);
        }
        const target = this.reg();
        this.emit(`  ${target} = getelementptr i64, i64* ${envPtr}, i32 ${index}`);
        this.emit(`  store i64 ${raw}, i64* ${target}`);
      }
    }
    const cast = this.reg();
    this.emit(`  ${cast} = bitcast i64 (i64, i32, i64*)* @${this.functionName(fn)} to i8*`);
    const closure = this.reg();
    this.emit(`  ${closure} = call i64 @xt_closure_new(i8* ${cast}, i32 ${fn.captures.length}, i64* ${envPtr})`);
    return closure;
  }

  // -- string pool ---------------------------------------------------------

  private stringValue(text: string): string {
    const entry = this.internString(text);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_string_new(i8* ${entry.label}, i64 ${entry.length})`);
    return result;
  }

  private internString(text: string): { label: string; length: number } {
    const existing = this.strings.get(text);
    if (existing) return existing;
    const label = `@.str.${this.stringCounter++}`;
    const bytes = utf8Bytes(text);
    const entry = { label, length: bytes.length };
    this.strings.set(text, entry);
    this.globals.push(`${label} = private unnamed_addr constant [${bytes.length + 1} x i8] c"${escapeBytes(bytes)}\\00"`);
    return entry;
  }

  private unsupported(node: Node, what: string): void {
    const label = kindName(node.kind);
    this.diagnostics.error(
      DiagnosticCode.UnsupportedFeature,
      `xbintsc does not yet support this ${what} (${label})`,
      node,
      this.sourceFile.fileName,
    );
  }
}

const BINARY_RUNTIME: Record<string, string | undefined> = {
  [BinaryOperator.Add]: "xt_add",
  [BinaryOperator.Subtract]: "xt_sub",
  [BinaryOperator.Multiply]: "xt_mul",
  [BinaryOperator.Divide]: "xt_div",
  [BinaryOperator.Remainder]: "xt_mod",
  [BinaryOperator.Exponent]: "xt_pow",
  [BinaryOperator.LessThan]: "xt_lt",
  [BinaryOperator.LessThanEquals]: "xt_le",
  [BinaryOperator.GreaterThan]: "xt_gt",
  [BinaryOperator.GreaterThanEquals]: "xt_ge",
  [BinaryOperator.EqualsEquals]: "xt_eq",
  [BinaryOperator.ExclamationEquals]: "xt_ne",
  [BinaryOperator.EqualsEqualsEquals]: "xt_seq",
  [BinaryOperator.ExclamationEqualsEquals]: "xt_sne",
  [BinaryOperator.Ampersand]: "xt_bit_and",
  [BinaryOperator.Bar]: "xt_bit_or",
  [BinaryOperator.Caret]: "xt_bit_xor",
  [BinaryOperator.LessThanLessThan]: "xt_shl",
  [BinaryOperator.GreaterThanGreaterThan]: "xt_shr",
  [BinaryOperator.GreaterThanGreaterThanGreaterThan]: "xt_ushr",
  [BinaryOperator.In]: "xt_in",
};

const CONSOLE_METHODS: Record<string, string> = {
  log: "xt_console_log",
  info: "xt_console_info",
  warn: "xt_console_warn",
  error: "xt_console_error",
};

const MATH_FUNCTIONS = new Set<string>([
  "abs", "floor", "ceil", "round", "trunc", "sqrt", "cbrt", "pow", "exp", "log", "log2", "log10",
  "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "hypot", "sign", "random", "min", "max",
]);

const MATH_CONSTANTS: Record<string, number> = {
  PI: Math.PI,
  E: Math.E,
  LN2: Math.LN2,
  LN10: Math.LN10,
  LOG2E: Math.LOG2E,
  LOG10E: Math.LOG10E,
  SQRT2: Math.SQRT2,
  SQRT1_2: Math.SQRT1_2,
};

const GLOBAL_FUNCTIONS: Record<string, string> = {
  parseInt: "xt_parse_int",
  parseFloat: "xt_parse_float",
  isNaN: "xt_is_nan",
  isFinite: "xt_is_finite",
  Number: "xt_number_ctor",
  String: "xt_string_ctor",
  Boolean: "xt_boolean_ctor",
};

const BUILTIN_METHODS = new Set<string>([
  "push", "pop", "shift", "unshift", "join", "slice", "indexOf", "includes", "map", "forEach", "filter",
  "reduce", "concat", "reverse", "charAt", "charCodeAt", "substring", "substr", "split", "toUpperCase",
  "toLowerCase", "trim", "replace", "repeat", "startsWith", "endsWith",
]);

const ASSIGNMENT_OPERATORS = new Set<string>([
  "=",
  "+=",
  "-=",
  "*=",
  "/=",
  "%=",
  "**=",
  "<<=",
  ">>=",
  ">>>=",
  "&=",
  "|=",
  "^=",
  "&&=",
  "||=",
  "??=",
]);

function isAssignmentOperator(operator: string): boolean {
  return ASSIGNMENT_OPERATORS.has(operator);
}

function compoundToBinary(operator: AssignmentOperator): BinaryOperator {
  const text = operator.slice(0, -1);
  return text as BinaryOperator;
}

function propertyNameText(name: Node): string {
  switch (name.kind) {
    case SyntaxKind.Identifier:
    case SyntaxKind.PrivateIdentifier:
      return (name as unknown as { text: string }).text;
    case SyntaxKind.StringLiteral:
      return (name as unknown as { value: string }).value;
    case SyntaxKind.NumericLiteral:
      return String((name as unknown as { value: number }).value);
    default:
      return "";
  }
}

/** Human readable syntax kind for diagnostics (const enums have no reverse map). */
function kindName(kind: number): string {
  const entry = KIND_NAMES.get(kind);
  return entry ?? String(kind);
}

const KIND_NAMES = new Map<number, string>([
  [SyntaxKind.CallExpression, "call expression"],
  [SyntaxKind.NewExpression, "new expression"],
  [SyntaxKind.ClassDeclaration, "class declaration"],
  [SyntaxKind.ClassExpression, "class expression"],
  [SyntaxKind.EnumDeclaration, "enum declaration"],
  [SyntaxKind.SwitchStatement, "switch statement"],
  [SyntaxKind.TryStatement, "try statement"],
  [SyntaxKind.SpreadElement, "spread element"],
  [SyntaxKind.ThisKeyword, "this expression"],
  [SyntaxKind.RegularExpressionLiteral, "regular expression"],
  [SyntaxKind.TaggedTemplateExpression, "tagged template"],
  [SyntaxKind.DeleteExpression, "delete expression"],
  [SyntaxKind.AwaitExpression, "await expression"],
  [SyntaxKind.YieldExpression, "yield expression"],
  [SyntaxKind.SwitchStatement, "switch statement"],
]);

function utf8Bytes(text: string): number[] {
  const bytes: number[] = [];
  for (const character of text) {
    const code = character.codePointAt(0)!;
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) {
      bytes.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
      bytes.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    } else {
      bytes.push(
        0xf0 | (code >> 18),
        0x80 | ((code >> 12) & 0x3f),
        0x80 | ((code >> 6) & 0x3f),
        0x80 | (code & 0x3f),
      );
    }
  }
  return bytes;
}

function escapeBytes(bytes: readonly number[]): string {
  return bytes
    .map((byte) => {
      if (byte === 0x22) return '\\22';
      if (byte === 0x5c) return '\\5C';
      if (byte >= 0x20 && byte < 0x7f) return String.fromCharCode(byte);
      return `\\${byte.toString(16).toUpperCase().padStart(2, "0")}`;
    })
    .join("");
}
