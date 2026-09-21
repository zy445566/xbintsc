/**
 * Control-flow lowering: `if`, `switch`, `try`/`catch`/`finally` and `return`.
 */

import {
  SyntaxKind,
  type CaseClause,
  type Identifier,
  type IfStatement,
  type ReturnStatement,
  type SwitchStatement,
  type TryStatement,
} from "../../../ast/nodes.js";
import { i64, XT_UNDEFINED } from "../../values.js";
import type { Generator } from "../generator.js";

export interface ControlFlowStatementMethods {
  emitIf(this: Generator, statement: IfStatement): void;
  emitSwitch(this: Generator, statement: SwitchStatement): void;
  emitTry(this: Generator, statement: TryStatement): void;
  bindCatchVariable(this: Generator, variable: Identifier | undefined, value: string): void;
  emitReturn(this: Generator, statement: ReturnStatement): void;
  popTryFramesTo(this: Generator, depth: number): void;
}

export const controlFlowStatementMethods: ControlFlowStatementMethods = {
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
