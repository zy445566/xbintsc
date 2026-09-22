/**
 * Loop lowering: `while`, `do`/`while`, `for` and `for`/`of` / `for`/`in`.
 */

import {
  SyntaxKind,
  type DoStatement,
  type Expression,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type VariableDeclarationList,
  type WhileStatement,
} from "../../../ast/nodes.js";
import { numberLiteral } from "../../values.js";
import type { Generator } from "../generator.js";
import type { LoopLabels } from "../state.js";

export interface LoopStatementMethods {
  emitWhile(this: Generator, statement: WhileStatement): void;
  emitDo(this: Generator, statement: DoStatement): void;
  emitFor(this: Generator, statement: ForStatement): void;
  emitForOf(this: Generator, statement: ForOfStatement | ForInStatement): void;
  bindLoopVariable(this: Generator, initializer: VariableDeclarationList | Expression, value: string): void;
  takePendingLabels(this: Generator): string[];
  findLoop(this: Generator, label: string | undefined): LoopLabels | undefined;
}

export const loopStatementMethods: LoopStatementMethods = {
  /** Consume labels accumulated by enclosing `LabeledStatement`s. */
  takePendingLabels(): string[] {
    return this.current.pendingLabels.splice(0, this.current.pendingLabels.length);
  },

  /** Innermost loop, or the innermost loop carrying `label`. */
  findLoop(label: string | undefined) {
    for (let i = this.current.loops.length - 1; i >= 0; i--) {
      const loop = this.current.loops[i]!;
      if (!label || loop.labels?.includes(label)) return loop;
    }
    return undefined;
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel, tryDepth: this.current.tryFrames.length, labels: this.takePendingLabels() });
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: condLabel, tryDepth: this.current.tryFrames.length, labels: this.takePendingLabels() });
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
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel, tryDepth: this.current.tryFrames.length, labels: this.takePendingLabels() });
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

    const condLabel = this.label("forof.cond");
    const bodyLabel = this.label("forof.body");
    const updateLabel = this.label("forof.update");
    const endLabel = this.label("forof.end");
    this.terminate(`br label %${condLabel}`);

    this.startBlock(condLabel);
    const index = this.reg();
    this.emit(`  ${index} = load i64, i64* ${indexPtr}`);
    /* `xt_iter_has` covers generators too, which cannot be sized up front. */
    const hasValue = this.reg();
    this.emit(`  ${hasValue} = call i64 @xt_iter_has(i64 ${iterable}, i64 ${index})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${hasValue})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
    this.terminate(`br i1 ${nonzero}, label %${bodyLabel}, label %${endLabel}`);

    this.startBlock(bodyLabel);
    const element = this.reg();
    this.emit(`  ${element} = call i64 @xt_iter_value(i64 ${iterable}, i64 ${index})`);
    this.bindLoopVariable(statement.initializer, element);
    this.current.loops.push({ breakLabel: endLabel, continueLabel: updateLabel, tryDepth: this.current.tryFrames.length, labels: this.takePendingLabels() });
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
};
