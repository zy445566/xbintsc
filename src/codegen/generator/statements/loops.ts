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

export interface LoopStatementMethods {
  emitWhile(this: Generator, statement: WhileStatement): void;
  emitDo(this: Generator, statement: DoStatement): void;
  emitFor(this: Generator, statement: ForStatement): void;
  emitForOf(this: Generator, statement: ForOfStatement | ForInStatement): void;
  bindLoopVariable(this: Generator, initializer: VariableDeclarationList | Expression, value: string): void;
}

export const loopStatementMethods: LoopStatementMethods = {
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
};
