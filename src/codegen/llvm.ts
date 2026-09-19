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
  "declare void @xt_console_log(i32, i64*)",
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
    const header = ["; ModuleID = 'xbtsc'", "source_filename = \"" + this.sourceFile.fileName + "\"", ""];
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
    for (let index = 0; index < fn.params.length; index++) {
      const symbol = fn.params[index]!;
      const value = this.reg();
      this.emit(`  ${value} = call i64 @xt_arg(i32 %argc, i64* %argv, i32 ${index})`);
      this.declareSlot(symbol, value);
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

    const lines = [...state.allocas.map((a) => `  ${a}`), ...state.buffer];
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
      case SyntaxKind.ReturnStatement:
        this.emitReturn(statement as ReturnStatement);
        return;
      case SyntaxKind.BreakStatement: {
        const loop = this.current.loops[this.current.loops.length - 1];
        if (loop) this.terminate(`br label %${loop.breakLabel}`);
        return;
      }
      case SyntaxKind.ContinueStatement: {
        const loop = this.current.loops[this.current.loops.length - 1];
        if (loop) this.terminate(`br label %${loop.continueLabel}`);
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel });
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel });
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel });
    this.emitStatement(statement.statement);
    this.current.loops.pop();
    if (!this.current.terminated) this.terminate(`br label %${updateLabel}`);

    this.startBlock(updateLabel);
    if (statement.incrementor) this.emitExpression(statement.incrementor);
    this.terminate(`br label %${condLabel}`);
    this.startBlock(endLabel);
  }

  /** `for (const x of xs)` / `for (const k in obj)` over arrays and objects. */
  private emitForOf(statement: ForOfStatement | ForInStatement): void {
    const iterable = this.emitExpression(statement.expression);
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel });
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

  private emitReturn(statement: ReturnStatement): void {
    const value = statement.expression ? this.emitExpression(statement.expression) : i64(XT_UNDEFINED);
    this.terminate(`ret i64 ${value}`);
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
    if (target.kind === SyntaxKind.Identifier && (target as Identifier).text === "console") {
      const args = this.emitArguments(node.arguments);
      this.emit(`  call void @xt_console_log(i32 ${args.argc}, i64* ${args.ptr})`);
      return i64(XT_UNDEFINED);
    }
    // Array and string methods.
    if (method === "push" || method === "pop" || method === "shift" || method === "unshift" || method === "join" || method === "slice" || method === "indexOf" || method === "includes" || method === "map" || method === "forEach" || method === "filter" || method === "reduce") {
      if (method === "push") {
        const object = this.emitExpression(target);
        const args = this.emitArguments(node.arguments);
        let last = object;
        if (args.argc === 0) return this.runtimeCall("xt_array_length", [object]);
        for (let index = 0; index < args.argc; index++) {
          const ptr = this.reg();
          this.emit(`  ${ptr} = getelementptr i64, i64* ${args.ptr}, i32 ${index}`);
          const element = this.reg();
          this.emit(`  ${element} = load i64, i64* ${ptr}`);
          const previous = last;
          last = this.reg();
          this.emit(`  ${last} = call i64 @xt_array_push(i64 ${previous}, i64 ${element})`);
        }
        return last;
      }
      this.unsupported(node, `array method '${method}'`);
      return i64(XT_UNDEFINED);
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

  private emitPropertyAccess(node: PropertyAccessExpression): string {
    if (node.name.text === "length") {
      const object = this.emitExpression(node.expression);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_array_length(i64 ${object})`);
      return result;
    }
    const object = this.emitExpression(node.expression);
    const key = this.stringValue(node.name.text);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
    return result;
  }

  private emitElementAccess(node: ElementAccessExpression): string {
    const object = this.emitExpression(node.expression);
    const key = this.emitExpression(node.argumentExpression);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
    return result;
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
      `xbtsc does not yet support this ${what} (${label})`,
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
};

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
