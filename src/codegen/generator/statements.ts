/**
 * Statement lowering: control flow, loops, switch and try/catch/finally.
 */

import {
  SyntaxKind,
  type ArrayBindingPattern,
  type BindingName,
  type Block,
  type CaseClause,
  type DoStatement,
  type EnumDeclaration,
  type Expression,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type Identifier,
  type IfStatement,
  type ObjectBindingPattern,
  type ReturnStatement,
  type Statement,
  type SwitchStatement,
  type ThrowStatement,
  type TryStatement,
  type VariableDeclarationList,
  type VariableStatement,
  type WhileStatement,
} from "../../ast/nodes.js";
import { i64, numberLiteral, XT_UNDEFINED } from "../values.js";
import { propertyNameText } from "./tables.js";
import type { Generator } from "./generator.js";

export interface StatementMethods {
  emitStatements(this: Generator, statements: readonly Statement[]): void;
  emitStatement(this: Generator, statement: Statement): void;
  emitVariableStatement(this: Generator, statement: VariableStatement): void;
  emitBindingPattern(this: Generator, name: BindingName, value: string): void;
  emitBindingDefault(this: Generator, value: string, initializer: Expression): string;
  emitEnum(this: Generator, statement: EnumDeclaration): void;
  emitIf(this: Generator, statement: IfStatement): void;
  emitWhile(this: Generator, statement: WhileStatement): void;
  emitDo(this: Generator, statement: DoStatement): void;
  emitFor(this: Generator, statement: ForStatement): void;
  emitForOf(this: Generator, statement: ForOfStatement | ForInStatement): void;
  bindLoopVariable(this: Generator, initializer: VariableDeclarationList | Expression, value: string): void;
  emitSwitch(this: Generator, statement: SwitchStatement): void;
  emitTry(this: Generator, statement: TryStatement): void;
  bindCatchVariable(this: Generator, variable: Identifier | undefined, value: string): void;
  emitReturn(this: Generator, statement: ReturnStatement): void;
  popTryFramesTo(this: Generator, depth: number): void;
}

export const statementMethods: StatementMethods = {
  emitStatements(statements: readonly Statement[]): void {
    for (const statement of statements) {
      if (this.current.terminated) break;
      this.emitStatement(statement);
    }
  },

  emitStatement(statement: Statement): void {
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
      case SyntaxKind.ImportDeclaration:
        return; // functions are emitted at module scope; imports are resolved at call sites
      case SyntaxKind.ClassDeclaration: {
        const info = this.binding.classOfNode.get(statement);
        if (info) this.emitClassSetup(info);
        return;
      }
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
      case SyntaxKind.EnumDeclaration:
        this.emitEnum(statement as EnumDeclaration);
        return;
      default:
        this.unsupported(statement, "statement");
    }
  },

  emitVariableStatement(statement: VariableStatement): void {
    for (const declaration of statement.declarationList.declarations) {
      if (declaration.name.kind === SyntaxKind.Identifier) {
        const symbol = this.binding.symbolOfDeclaration.get(declaration);
        if (!symbol) {
          if (declaration.initializer) this.emitExpression(declaration.initializer);
          continue;
        }
        if (this.current.slots.has(symbol.id)) {
          // Hoisted `var` (or an already-declared slot): keep evaluating the
          // initializer for its side effects, but do not redeclare the slot.
          if (declaration.initializer) this.emitExpression(declaration.initializer);
          continue;
        }
        if (symbol.boxed) {
          // A captured binding lives in a box. Create the box *before*
          // evaluating the initializer so a self-referencing closure
          // (`const f = () => f()`) captures the box, not a copy of the
          // still-uninitialised value.
          this.declareSlot(symbol, i64(XT_UNDEFINED));
          if (declaration.initializer) this.writeSlot(symbol, this.emitExpression(declaration.initializer));
          continue;
        }
        const initial = declaration.initializer ? this.emitExpression(declaration.initializer) : i64(XT_UNDEFINED);
        this.declareSlot(symbol, initial);
      } else {
        const initial = declaration.initializer ? this.emitExpression(declaration.initializer) : i64(XT_UNDEFINED);
        this.emitBindingPattern(declaration.name, initial);
      }
    }
  },

  /** Lower a destructuring binding pattern, declaring each bound identifier. */
  emitBindingPattern(name: BindingName, value: string): void {
    if (name.kind === SyntaxKind.Identifier) {
      const symbol = this.binding.symbolOfDeclaration.get(name);
      if (!symbol) return;
      if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
      else this.declareSlot(symbol, value);
      return;
    }
    if (name.kind === SyntaxKind.ArrayBindingPattern) {
      const pattern = name as ArrayBindingPattern;
      for (let index = 0; index < pattern.elements.length; index++) {
        const element = pattern.elements[index];
        if (!element) continue;
        let elementValue: string;
        if (element.dotDotDotToken) {
          const startPtr = this.alloca();
          this.emit(`  store i64 ${numberLiteral(index)}, i64* ${startPtr}`);
          const sliceName = this.stringValue("slice");
          elementValue = this.reg();
          this.emit(`  ${elementValue} = call i64 @xt_call_method(i64 ${value}, i64 ${sliceName}, i32 1, i64* ${startPtr})`);
        } else {
          elementValue = this.reg();
          this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${numberLiteral(index)})`);
        }
        if (element.initializer) elementValue = this.emitBindingDefault(elementValue, element.initializer);
        this.emitBindingPattern(element.name, elementValue);
      }
      return;
    }
    const pattern = name as ObjectBindingPattern;
    for (const element of pattern.elements) {
      const key = element.propertyName ?? (element.name.kind === SyntaxKind.Identifier ? element.name : undefined);
      if (element.dotDotDotToken) {
        this.unsupported(element, "object rest destructuring");
        continue;
      }
      const keyValue = this.stringValue(key ? propertyNameText(key) : "undefined");
      let elementValue = this.reg();
      this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${keyValue})`);
      if (element.initializer) elementValue = this.emitBindingDefault(elementValue, element.initializer);
      this.emitBindingPattern(element.name, elementValue);
    }
  },

  /** `x = fallback` in a binding pattern: only apply when `x` is `undefined`. */
  emitBindingDefault(value: string, initializer: Expression): string {
    const fallback = this.emitExpression(initializer);
    const isUndefined = this.reg();
    this.emit(`  ${isUndefined} = call i64 @xt_seq(i64 ${value}, i64 ${i64(XT_UNDEFINED)})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${isUndefined})`);
    const condition = this.reg();
    this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    const result = this.reg();
    this.emit(`  ${result} = select i1 ${condition}, i64 ${fallback}, i64 ${value}`);
    return result;
  },

  /**
   * `enum E { A, B }` (and `const enum`) lowers to a runtime object with both
   * forward (`E.A`) and reverse (`E[0] === "A"`) numeric mappings.
   */
  emitEnum(statement: EnumDeclaration): void {
    const symbol = this.binding.symbolOfDeclaration.get(statement);
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    let next = 0;
    for (const member of statement.members) {
      const name = propertyNameText(member.name);
      const key = this.stringValue(name);
      let value: string;
      if (member.initializer) {
        value = this.emitExpression(member.initializer);
        if (member.initializer.kind === SyntaxKind.NumericLiteral) {
          const numeric = (member.initializer as { value?: number }).value ?? 0;
          next = numeric + 1;
        } else {
          next++; 
        }
      } else {
        value = numberLiteral(next);
        next++;
      }
      this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      // Reverse mapping (only meaningful for numeric members).
      if (!member.initializer || member.initializer.kind === SyntaxKind.NumericLiteral) {
        const reverseKey = this.stringValue(name);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${value}, i64 ${reverseKey})`);
      }
    }
    if (symbol) this.declareSlot(symbol, object);
  },

  emitIf(statement: IfStatement): void {
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
  },

  emitWhile(statement: WhileStatement): void {
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
  },

  emitDo(statement: DoStatement): void {
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
  },

  emitFor(statement: ForStatement): void {
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
  },

  /** `for (const x of xs)` / `for (const k in obj)` over arrays, strings and objects. */
  emitForOf(statement: ForOfStatement | ForInStatement): void {
    const source = this.emitExpression(statement.expression);
    const isForIn = statement.kind === SyntaxKind.ForInStatement;
    // `for...in` iterates the enumerable keys (indices become strings);
    // `for...of` iterates the values at each index.
    const iterable = isForIn ? this.runtimeCall("xt_object_keys", [source]) : source;
    const indexPtr = this.alloca();
    this.emit(`  store i64 ${numberLiteral(0)}, i64* ${indexPtr}`);
    const lengthValue = this.reg();
    this.emit(`  ${lengthValue} = call i64 @xt_iter_length(i64 ${iterable})`);

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
    this.emit(`  ${element} = call i64 @xt_iter_value(i64 ${iterable}, i64 ${index})`);
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
  },

  bindLoopVariable(initializer: VariableDeclarationList | Expression, value: string): void {
    if (initializer.kind === SyntaxKind.VariableDeclarationList) {
      const declaration = initializer.declarations[0];
      if (!declaration) return;
      if (declaration.name.kind !== SyntaxKind.Identifier) {
        this.emitBindingPattern(declaration.name, value);
        return;
      }
      const symbol = this.binding.symbolOfDeclaration.get(declaration) ?? this.binding.symbolOfDeclaration.get(declaration.name);
      if (symbol) {
        if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
        else this.declareSlot(symbol, value);
      }
      return;
    }
    this.emitAssignmentTarget(initializer as Expression, value);
  },

  /**
   * JavaScript `switch`: test each `case` with strict equality, then run the
   * matched clause and fall through into the following clauses until `break`.
   */
  emitSwitch(statement: SwitchStatement): void {
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
  },

  /**
   * `try`/`catch`/`finally` via a runtime setjmp frame. The runtime keeps a
   * stack of frames; `xt_throw` longjmps into the innermost one. A `return`
   * that exits the protected region skips `finally` (a known limitation).
   */
  emitTry(statement: TryStatement): void {
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
    // MSVC's `_setjmp` takes the caller's frame address as a second argument
    // (`_JUMP_BUFFER.Frame`); `longjmp` uses it to run the unwind. clang lowers
    // a C call to `_setjmp` exactly like this. The extra argument is ignored by
    // the one-argument `_setjmp` on Linux/macOS.
    const frameAddress = this.reg();
    this.emit(`  ${frameAddress} = call i8* @llvm.frameaddress(i32 0)`);
    const jump = this.reg();
    this.emit(`  ${jump} = call i32 @_setjmp(i8* ${frame}, i8* ${frameAddress})`);
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
  },

  bindCatchVariable(variable: Identifier | undefined, value: string): void {
    if (!variable) return;
    const symbol = this.binding.symbolOfDeclaration.get(variable);
    if (!symbol) return;
    if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
    else this.declareSlot(symbol, value);
  },

  emitReturn(statement: ReturnStatement): void {
    let value = statement.expression ? this.emitExpression(statement.expression) : i64(XT_UNDEFINED);
    if (this.current.fn.isAsync) value = this.wrapAsync(value);
    this.popTryFramesTo(0);
    this.terminate(`ret i64 ${value}`);
  },

  /** Emit `xt_try_leave` for every active frame above `depth` (innermost first). */
  popTryFramesTo(depth: number): void {
    for (let index = this.current.tryFrames.length - 1; index >= depth; index--) {
      const slot = this.current.tryFrames[index]!;
      const frame = this.reg();
      this.emit(`  ${frame} = load i8*, i8** ${slot}`);
      this.emit(`  call void @xt_try_leave(i8* ${frame})`);
    }
  },
};
