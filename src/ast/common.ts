/**
 * Core AST shapes shared by every node category: source ranges, the base
 * `Node` interface, the `Modifier` node and the `Expression`/`Statement`/
 * `TypeNode` discriminated unions.
 */

import { SyntaxKind, type ModifierKind } from "./kinds.js";
import type {
  ArrayLiteralExpression,
  ArrowFunction,
  AsExpression,
  AwaitExpression,
  BigIntLiteral,
  BinaryExpression,
  BooleanLiteral,
  CallExpression,
  ClassExpression,
  ConditionalExpression,
  DeleteExpression,
  ElementAccessExpression,
  FunctionExpression,
  Identifier,
  NewExpression,
  NoSubstitutionTemplateLiteral,
  NonNullExpression,
  NullLiteral,
  NumericLiteral,
  ObjectLiteralExpression,
  ParenthesizedExpression,
  PostfixUnaryExpression,
  PrefixUnaryExpression,
  PrivateIdentifier,
  PropertyAccessExpression,
  RegularExpressionLiteral,
  SatisfiesExpression,
  SpreadElement,
  StringLiteral,
  TaggedTemplateExpression,
  TemplateLiteral,
  ThisExpression,
  TypeOfExpression,
  UndefinedLiteral,
  VoidExpression,
  YieldExpression,
} from "./expressions.js";
import type {
  Block,
  BreakStatement,
  ContinueStatement,
  DebuggerStatement,
  DoStatement,
  EmptyStatement,
  ExpressionStatement,
  ForInStatement,
  ForOfStatement,
  ForStatement,
  IfStatement,
  LabeledStatement,
  ReturnStatement,
  SwitchStatement,
  ThrowStatement,
  TryStatement,
  VariableStatement,
  WhileStatement,
} from "./statements.js";
import type {
  ClassDeclaration,
  EnumDeclaration,
  FunctionDeclaration,
  InterfaceDeclaration,
  ModuleDeclaration,
  TypeAliasDeclaration,
} from "./declarations.js";
import type {
  ExportAssignment,
  ExportDeclaration,
  ImportDeclaration,
} from "./modules.js";
import type {
  ArrayTypeNode,
  ConditionalTypeNode,
  FunctionTypeNode,
  IndexedAccessTypeNode,
  InferTypeNode,
  IntersectionTypeNode,
  KeywordTypeNode,
  LiteralTypeNode,
  MappedTypeNode,
  OptionalTypeNode,
  ParenthesizedTypeNode,
  RestTypeNode,
  TupleTypeNode,
  TypeLiteralNode,
  TypeOperatorNode,
  TypePredicate,
  TypeQueryNode,
  TypeReference,
  UnionTypeNode,
} from "./types.js";

export interface Range {
  /** Inclusive start offset (UTF-16 code units). */
  readonly start: number;
  /** Exclusive end offset. */
  readonly end: number;
}

export interface Node extends Range {
  readonly kind: SyntaxKind;
}

export interface Modifier extends Node {
  readonly kind: SyntaxKind.Unknown;
  readonly modifierKind: ModifierKind;
}

export type Expression =
  | Identifier
  | PrivateIdentifier
  | NumericLiteral
  | BigIntLiteral
  | StringLiteral
  | NoSubstitutionTemplateLiteral
  | TemplateLiteral
  | RegularExpressionLiteral
  | BooleanLiteral
  | NullLiteral
  | UndefinedLiteral
  | ThisExpression
  | ArrayLiteralExpression
  | ObjectLiteralExpression
  | FunctionExpression
  | ArrowFunction
  | ClassExpression
  | CallExpression
  | NewExpression
  | PropertyAccessExpression
  | ElementAccessExpression
  | ParenthesizedExpression
  | PrefixUnaryExpression
  | PostfixUnaryExpression
  | BinaryExpression
  | ConditionalExpression
  | AsExpression
  | SatisfiesExpression
  | NonNullExpression
  | TypeOfExpression
  | VoidExpression
  | DeleteExpression
  | AwaitExpression
  | YieldExpression
  | TaggedTemplateExpression
  | SpreadElement;

export type Statement =
  | ExpressionStatement
  | Block
  | EmptyStatement
  | DebuggerStatement
  | VariableStatement
  | IfStatement
  | WhileStatement
  | DoStatement
  | ForStatement
  | ForOfStatement
  | ForInStatement
  | ReturnStatement
  | BreakStatement
  | ContinueStatement
  | ThrowStatement
  | TryStatement
  | SwitchStatement
  | LabeledStatement
  | FunctionDeclaration
  | ClassDeclaration
  | InterfaceDeclaration
  | TypeAliasDeclaration
  | EnumDeclaration
  | ModuleDeclaration
  | ImportDeclaration
  | ExportDeclaration
  | ExportAssignment;

export type TypeNode =
  | TypeReference
  | TypePredicate
  | KeywordTypeNode
  | LiteralTypeNode
  | ArrayTypeNode
  | TupleTypeNode
  | UnionTypeNode
  | IntersectionTypeNode
  | FunctionTypeNode
  | TypeLiteralNode
  | ParenthesizedTypeNode
  | TypeQueryNode
  | IndexedAccessTypeNode
  | TypeOperatorNode
  | MappedTypeNode
  | ConditionalTypeNode
  | InferTypeNode
  | OptionalTypeNode
  | RestTypeNode;

export interface SourceFileNode extends Node {
  readonly kind: SyntaxKind.SourceFile;
  readonly statements: Statement[];
  readonly fileName: string;
  readonly text: string;
}
