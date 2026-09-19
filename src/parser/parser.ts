/**
 * Parser entry point.
 *
 * The actual parsing logic lives in focused modules (`statements`,
 * `expressions`, `declarations`, `modules`, `types`) and is installed onto
 * {@link Parser} via `Object.assign`. `ParserContext` supplies the shared
 * scanner/token state and helpers. This file keeps the public `Parser` class,
 * which downstream code (and tests) continue to import from here.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { TokenKind } from "../lexer/token.js";
import { SyntaxKind, type SourceFileNode, type Statement } from "../ast/nodes.js";
import { ParserContext } from "./context.js";
import { statementMethods, type StatementMethods } from "./statements.js";
import { expressionMethods, type ExpressionMethods } from "./expressions.js";
import { literalMethods, type LiteralMethods } from "./literals.js";
import { declarationMethods, type DeclarationMethods } from "./declarations.js";
import { moduleMethods, type ModuleMethods } from "./modules.js";
import { typeMethods, type TypeMethods } from "./types.js";

export class Parser extends ParserContext {
  parseSourceFile(): SourceFileNode {
    const statements: Statement[] = [];
    while (!this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const beforePos = this.scanner.position;
      const statement = this.parseStatement();
      if (statement) statements.push(statement);
      if (this.consumed === before) {
        this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${this.token.text}'`, this.token);
        this.nextToken();
      }
    }
    return {
      kind: SyntaxKind.SourceFile,
      statements,
      fileName: this.source.fileName,
      text: this.source.text,
      start: 0,
      end: this.source.text.length,
    };
  }
}

export interface Parser
  extends StatementMethods,
    ExpressionMethods,
    LiteralMethods,
    DeclarationMethods,
    ModuleMethods,
    TypeMethods {}

Object.assign(
  Parser.prototype,
  statementMethods,
  expressionMethods,
  literalMethods,
  declarationMethods,
  moduleMethods,
  typeMethods,
);
