/**
 * Statement nodes of the xbintsc AST.
 */

import { SyntaxKind } from "./kinds.js";
import type { Expression, Modifier, Node, Statement, TypeNode } from "./common.js";
import type { Identifier } from "./expressions.js";
import type { BindingName } from "./declarations.js";

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

export interface ExpressionStatement extends Node {
  readonly kind: SyntaxKind.ExpressionStatement;
  readonly expression: Expression;
}

export interface Block extends Node {
  readonly kind: SyntaxKind.Block;
  readonly statements: Statement[];
}

export interface EmptyStatement extends Node {
  readonly kind: SyntaxKind.EmptyStatement;
}

export interface DebuggerStatement extends Node {
  readonly kind: SyntaxKind.DebuggerStatement;
}

export interface VariableDeclaration extends Node {
  readonly kind: SyntaxKind.VariableDeclaration;
  readonly name: BindingName;
  readonly exclamation: boolean;
  readonly type?: TypeNode;
  readonly initializer?: Expression;
}

export interface VariableDeclarationList extends Node {
  readonly kind: SyntaxKind.VariableDeclarationList;
  readonly declarations: VariableDeclaration[];
  readonly declarationKind: "var" | "let" | "const" | "using";
}

export interface VariableStatement extends Node {
  readonly kind: SyntaxKind.VariableStatement;
  readonly declarationList: VariableDeclarationList;
  readonly modifiers: Modifier[];
}

export interface IfStatement extends Node {
  readonly kind: SyntaxKind.IfStatement;
  readonly condition: Expression;
  readonly thenStatement: Statement;
  readonly elseStatement?: Statement;
}

export interface WhileStatement extends Node {
  readonly kind: SyntaxKind.WhileStatement;
  readonly condition: Expression;
  readonly statement: Statement;
}

export interface DoStatement extends Node {
  readonly kind: SyntaxKind.DoStatement;
  readonly condition: Expression;
  readonly statement: Statement;
}

export interface ForStatement extends Node {
  readonly kind: SyntaxKind.ForStatement;
  readonly initializer?: VariableDeclarationList | Expression;
  readonly condition?: Expression;
  readonly incrementor?: Expression;
  readonly statement: Statement;
}

export interface ForOfStatement extends Node {
  readonly kind: SyntaxKind.ForOfStatement;
  readonly initializer: VariableDeclarationList | Expression;
  readonly expression: Expression;
  readonly statement: Statement;
  readonly awaitModifier: boolean;
}

export interface ForInStatement extends Node {
  readonly kind: SyntaxKind.ForInStatement;
  readonly initializer: VariableDeclarationList | Expression;
  readonly expression: Expression;
  readonly statement: Statement;
}

export interface ReturnStatement extends Node {
  readonly kind: SyntaxKind.ReturnStatement;
  readonly expression?: Expression;
}

export interface BreakStatement extends Node {
  readonly kind: SyntaxKind.BreakStatement;
  readonly label?: Identifier;
}

export interface ContinueStatement extends Node {
  readonly kind: SyntaxKind.ContinueStatement;
  readonly label?: Identifier;
}

export interface ThrowStatement extends Node {
  readonly kind: SyntaxKind.ThrowStatement;
  readonly expression: Expression;
}

export interface CatchClause extends Node {
  readonly kind: SyntaxKind.CatchClause;
  readonly variable?: Identifier;
  readonly type?: TypeNode;
  readonly block: Block;
}

export interface TryStatement extends Node {
  readonly kind: SyntaxKind.TryStatement;
  readonly tryBlock: Block;
  readonly catchClause?: CatchClause;
  readonly finallyBlock?: Block;
}

export interface CaseClause extends Node {
  readonly kind: SyntaxKind.CaseClause;
  readonly expression: Expression;
  readonly statements: Statement[];
}

export interface DefaultClause extends Node {
  readonly kind: SyntaxKind.DefaultClause;
  readonly statements: Statement[];
}

export interface SwitchStatement extends Node {
  readonly kind: SyntaxKind.SwitchStatement;
  readonly expression: Expression;
  readonly clauses: (CaseClause | DefaultClause)[];
}

export interface LabeledStatement extends Node {
  readonly kind: SyntaxKind.LabeledStatement;
  readonly label: Identifier;
  readonly statement: Statement;
}
