/**
 * Statement dispatch: walks a statement list and routes each kind to its
 * lowering method. The concrete lowerings live in the sibling modules
 * (`variables`, `control-flow`, `loops`).
 */

import {
  SyntaxKind,
  type Block,
  type DoStatement,
  type EnumDeclaration,
  type Expression,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type IfStatement,
  type ReturnStatement,
  type Statement,
  type SwitchStatement,
  type ThrowStatement,
  type TryStatement,
  type VariableStatement,
  type WhileStatement,
} from "../../../ast/nodes.js";
import type { Generator } from "../generator.js";

export interface StatementDispatchMethods {
  emitStatements(this: Generator, statements: readonly Statement[]): void;
  emitStatement(this: Generator, statement: Statement): void;
}

export const statementDispatchMethods: StatementDispatchMethods = {
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
};
