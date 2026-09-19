/**
 * The xtsc abstract syntax tree.
 *
 * The AST is a faithful, lossless-enough representation of the TypeScript
 * source. Nodes are discriminated unions keyed by `kind` so the binder, checker
 * and code generator can switch exhaustively over them. Every node carries a
 * source range (`start`/`end` offsets) which the diagnostics layer maps back to
 * line/column, and which the incremental cache uses to detect changes.
 */

import type { Token } from "../lexer/token.js";

export interface Range {
  /** Inclusive start offset (UTF-16 code units). */
  readonly start: number;
  /** Exclusive end offset. */
  readonly end: number;
}

export interface Node extends Range {
  readonly kind: SyntaxKind;
}

export const enum SyntaxKind {
  // Markers
  Unknown = 0,
  EndOfFileToken = 1,
  SourceFile = 300,
  QualifiedName = 301,
  ExpressionWithTypeArguments = 302,
  ImportType = 303,

  // Trivia-ish
  Identifier = 2,
  PrivateIdentifier = 3,

  // Literals
  NumericLiteral = 4,
  BigIntLiteral = 5,
  StringLiteral = 6,
  NoSubstitutionTemplateLiteral = 7,
  TemplateLiteral = 8,
  TemplateSpan = 9,
  RegularExpressionLiteral = 10,
  TrueKeyword = 11,
  FalseKeyword = 12,
  NullKeyword = 13,
  UndefinedKeyword = 14,
  ThisKeyword = 15,

  // Expressions
  ArrayLiteralExpression = 20,
  ObjectLiteralExpression = 21,
  PropertyAssignment = 22,
  ShorthandPropertyAssignment = 23,
  SpreadElement = 24,
  FunctionExpression = 25,
  ArrowFunction = 26,
  ClassExpression = 27,
  CallExpression = 28,
  NewExpression = 29,
  PropertyAccessExpression = 30,
  ElementAccessExpression = 31,
  ParenthesizedExpression = 32,
  PrefixUnaryExpression = 33,
  PostfixUnaryExpression = 34,
  BinaryExpression = 35,
  ConditionalExpression = 36,
  AsExpression = 37,
  SatisfiesExpression = 38,
  NonNullExpression = 39,
  TypeOfExpression = 40,
  VoidExpression = 41,
  DeleteExpression = 42,
  AwaitExpression = 43,
  YieldExpression = 44,
  TaggedTemplateExpression = 45,

  // Statements
  ExpressionStatement = 60,
  Block = 61,
  EmptyStatement = 62,
  DebuggerStatement = 63,
  VariableStatement = 64,
  VariableDeclarationList = 65,
  VariableDeclaration = 66,
  IfStatement = 67,
  WhileStatement = 68,
  DoStatement = 69,
  ForStatement = 70,
  ForOfStatement = 71,
  ForInStatement = 72,
  ReturnStatement = 73,
  BreakStatement = 74,
  ContinueStatement = 75,
  ThrowStatement = 76,
  TryStatement = 77,
  CatchClause = 78,
  SwitchStatement = 79,
  CaseClause = 80,
  DefaultClause = 81,
  LabeledStatement = 82,

  // Declarations
  FunctionDeclaration = 100,
  ClassDeclaration = 101,
  InterfaceDeclaration = 102,
  TypeAliasDeclaration = 103,
  EnumDeclaration = 104,
  EnumMember = 105,
  ModuleDeclaration = 106,
  Parameter = 107,
  TypeParameterDeclaration = 108,
  PropertyDeclaration = 109,
  MethodDeclaration = 110,
  ConstructorDeclaration = 111,
  HeritageClause = 112,
  ImportDeclaration = 113,
  ImportClause = 114,
  NamedImports = 115,
  ImportSpecifier = 116,
  NamespaceImport = 117,
  ExportDeclaration = 118,
  ExportAssignment = 119,
  NamedExports = 120,
  ExportSpecifier = 121,

  // Types
  TypeReference = 200,
  TypePredicate = 201,
  AnyKeywordType = 202,
  UnknownKeywordType = 203,
  NumberKeywordType = 204,
  BigIntKeywordType = 205,
  StringKeywordType = 206,
  BooleanKeywordType = 207,
  SymbolKeywordType = 208,
  VoidKeywordType = 209,
  UndefinedKeywordType = 210,
  NullKeywordType = 211,
  NeverKeywordType = 212,
  ObjectKeywordType = 213,
  ThisKeywordType = 214,
  LiteralType = 215,
  ArrayType = 216,
  TupleType = 217,
  UnionType = 218,
  IntersectionType = 219,
  FunctionType = 220,
  TypeLiteral = 221,
  PropertySignature = 222,
  MethodSignature = 223,
  IndexSignature = 224,
  ParenthesizedType = 225,
  TypeQuery = 226,
  IndexedAccessType = 227,
  TypeOperator = 228,
  MappedType = 229,
  ConditionalType = 230,
  InferType = 231,
  OptionalType = 232,
  RestType = 233,
}

export const enum NodeFlags {
  None = 0,
  Let = 1 << 0,
  Const = 1 << 1,
  Var = 1 << 2,
  Exported = 1 << 3,
  Default = 1 << 4,
  Declare = 1 << 5,
  Async = 1 << 6,
  Generator = 1 << 7,
  Ambient = 1 << 8,
  Readonly = 1 << 9,
  Abstract = 1 << 10,
  Optional = 1 << 11,
}

export const enum ModifierKind {
  Export = "export",
  Default = "default",
  Declare = "declare",
  Abstract = "abstract",
  Public = "public",
  Private = "private",
  Protected = "protected",
  Static = "static",
  Readonly = "readonly",
  Async = "async",
}

export interface Modifier extends Node {
  readonly kind: SyntaxKind.Unknown;
  readonly modifierKind: ModifierKind;
}

// ---------------------------------------------------------------------------
// Base node shapes
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Identifiers and literals
// ---------------------------------------------------------------------------

export interface Identifier extends Node {
  readonly kind: SyntaxKind.Identifier;
  readonly text: string;
  /** True when the identifier was written with a `#` prefix. */
  readonly escaped?: boolean;
}

export interface PrivateIdentifier extends Node {
  readonly kind: SyntaxKind.PrivateIdentifier;
  readonly text: string;
}

export interface NumericLiteral extends Node {
  readonly kind: SyntaxKind.NumericLiteral;
  readonly text: string;
  readonly value: number;
}

export interface BigIntLiteral extends Node {
  readonly kind: SyntaxKind.BigIntLiteral;
  readonly text: string;
  readonly value: bigint;
}

export interface StringLiteral extends Node {
  readonly kind: SyntaxKind.StringLiteral;
  readonly text: string;
  readonly value: string;
  /** Raw source text, including quotes. */
  readonly raw: string;
}

export interface NoSubstitutionTemplateLiteral extends Node {
  readonly kind: SyntaxKind.NoSubstitutionTemplateLiteral;
  readonly text: string;
  readonly value: string;
}

export interface TemplateLiteral extends Node {
  readonly kind: SyntaxKind.TemplateLiteral;
  readonly head: string;
  readonly spans: TemplateSpan[];
}

export interface TemplateSpan extends Node {
  readonly kind: SyntaxKind.TemplateSpan;
  readonly expression: Expression;
  readonly literal: string;
  /** True when this span terminates the template (tail) rather than continues it. */
  readonly isTail: boolean;
}

export interface RegularExpressionLiteral extends Node {
  readonly kind: SyntaxKind.RegularExpressionLiteral;
  readonly text: string;
  readonly pattern: string;
  readonly flags: string;
}

export interface BooleanLiteral extends Node {
  readonly kind: SyntaxKind.TrueKeyword | SyntaxKind.FalseKeyword;
  readonly value: boolean;
}

export interface NullLiteral extends Node {
  readonly kind: SyntaxKind.NullKeyword;
}

export interface UndefinedLiteral extends Node {
  readonly kind: SyntaxKind.UndefinedKeyword;
}

export interface ThisExpression extends Node {
  readonly kind: SyntaxKind.ThisKeyword;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

export interface ArrayLiteralExpression extends Node {
  readonly kind: SyntaxKind.ArrayLiteralExpression;
  readonly elements: Expression[];
}

export interface PropertyAssignment extends Node {
  readonly kind: SyntaxKind.PropertyAssignment;
  readonly name: PropertyName;
  readonly initializer: Expression;
}

export interface ShorthandPropertyAssignment extends Node {
  readonly kind: SyntaxKind.ShorthandPropertyAssignment;
  readonly name: Identifier;
}

export interface SpreadElement extends Node {
  readonly kind: SyntaxKind.SpreadElement;
  readonly expression: Expression;
}

export type ObjectLiteralElementLike = PropertyAssignment | ShorthandPropertyAssignment | SpreadElement;

export interface ObjectLiteralExpression extends Node {
  readonly kind: SyntaxKind.ObjectLiteralExpression;
  readonly properties: ObjectLiteralElementLike[];
}

export type PropertyName = Identifier | StringLiteral | NumericLiteral | PrivateIdentifier;

export interface FunctionExpression extends Node {
  readonly kind: SyntaxKind.FunctionExpression;
  readonly name?: Identifier;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body: Block;
  readonly flags: NodeFlags;
}

export interface ArrowFunction extends Node {
  readonly kind: SyntaxKind.ArrowFunction;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body: Block | Expression;
  readonly flags: NodeFlags;
}

export interface ClassExpression extends Node {
  readonly kind: SyntaxKind.ClassExpression;
  readonly name?: Identifier;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: ClassElement[];
}

export interface CallExpression extends Node {
  readonly kind: SyntaxKind.CallExpression;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
  readonly arguments: Expression[];
  readonly optional: boolean;
}

export interface NewExpression extends Node {
  readonly kind: SyntaxKind.NewExpression;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
  readonly arguments: Expression[];
}

export interface PropertyAccessExpression extends Node {
  readonly kind: SyntaxKind.PropertyAccessExpression;
  readonly expression: Expression;
  readonly name: Identifier;
  readonly optional: boolean;
}

export interface ElementAccessExpression extends Node {
  readonly kind: SyntaxKind.ElementAccessExpression;
  readonly expression: Expression;
  readonly argumentExpression: Expression;
  readonly optional: boolean;
}

export interface ParenthesizedExpression extends Node {
  readonly kind: SyntaxKind.ParenthesizedExpression;
  readonly expression: Expression;
}

export const enum PrefixUnaryOperator {
  Plus = "+",
  Minus = "-",
  Tilde = "~",
  Exclamation = "!",
  TypeOf = "typeof",
  Void = "void",
  Delete = "delete",
  PlusPlus = "++",
  MinusMinus = "--",
  Await = "await",
}

export interface PrefixUnaryExpression extends Node {
  readonly kind: SyntaxKind.PrefixUnaryExpression;
  readonly operator: PrefixUnaryOperator;
  readonly operand: Expression;
}

export const enum PostfixUnaryOperator {
  PlusPlus = "++",
  MinusMinus = "--",
}

export interface PostfixUnaryExpression extends Node {
  readonly kind: SyntaxKind.PostfixUnaryExpression;
  readonly operator: PostfixUnaryOperator;
  readonly operand: Expression;
}

export const enum BinaryOperator {
  Add = "+",
  Subtract = "-",
  Multiply = "*",
  Divide = "/",
  Remainder = "%",
  Exponent = "**",
  LessThan = "<",
  LessThanEquals = "<=",
  GreaterThan = ">",
  GreaterThanEquals = ">=",
  EqualsEquals = "==",
  ExclamationEquals = "!=",
  EqualsEqualsEquals = "===",
  ExclamationEqualsEquals = "!==",
  AmpersandAmpersand = "&&",
  BarBar = "||",
  QuestionQuestion = "??",
  Ampersand = "&",
  Bar = "|",
  Caret = "^",
  LessThanLessThan = "<<",
  GreaterThanGreaterThan = ">>",
  GreaterThanGreaterThanGreaterThan = ">>>",
  Comma = ",",
  In = "in",
  InstanceOf = "instanceof",
}

/** Assignment operators, including the compound forms. */
export const enum AssignmentOperator {
  Assign = "=",
  AddAssign = "+=",
  SubtractAssign = "-=",
  MultiplyAssign = "*=",
  DivideAssign = "/=",
  RemainderAssign = "%=",
  ExponentAssign = "**=",
  LessThanLessThanAssign = "<<=",
  GreaterThanGreaterThanAssign = ">>=",
  GreaterThanGreaterThanGreaterThanAssign = ">>>=",
  AmpersandAssign = "&=",
  BarAssign = "|=",
  CaretAssign = "^=",
  AmpersandAmpersandAssign = "&&=",
  BarBarAssign = "||=",
  QuestionQuestionAssign = "??=",
}

export interface BinaryExpression extends Node {
  readonly kind: SyntaxKind.BinaryExpression;
  readonly left: Expression;
  readonly operator: BinaryOperator;
  readonly right: Expression;
  readonly operatorToken: Token;
}

export interface AssignmentExpression extends Node {
  readonly kind: SyntaxKind.BinaryExpression;
  readonly left: Expression;
  readonly operator: AssignmentOperator;
  readonly right: Expression;
  readonly operatorToken: Token;
}

export interface ConditionalExpression extends Node {
  readonly kind: SyntaxKind.ConditionalExpression;
  readonly condition: Expression;
  readonly whenTrue: Expression;
  readonly whenFalse: Expression;
}

export interface AsExpression extends Node {
  readonly kind: SyntaxKind.AsExpression;
  readonly expression: Expression;
  readonly type: TypeNode;
}

export interface SatisfiesExpression extends Node {
  readonly kind: SyntaxKind.SatisfiesExpression;
  readonly expression: Expression;
  readonly type: TypeNode;
}

export interface NonNullExpression extends Node {
  readonly kind: SyntaxKind.NonNullExpression;
  readonly expression: Expression;
}

export interface TypeOfExpression extends Node {
  readonly kind: SyntaxKind.TypeOfExpression;
  readonly expression: Expression;
}

export interface VoidExpression extends Node {
  readonly kind: SyntaxKind.VoidExpression;
  readonly expression?: Expression;
}

export interface DeleteExpression extends Node {
  readonly kind: SyntaxKind.DeleteExpression;
  readonly expression: Expression;
}

export interface AwaitExpression extends Node {
  readonly kind: SyntaxKind.AwaitExpression;
  readonly expression: Expression;
}

export interface YieldExpression extends Node {
  readonly kind: SyntaxKind.YieldExpression;
  readonly expression?: Expression;
  readonly delegate: boolean;
}

export interface TaggedTemplateExpression extends Node {
  readonly kind: SyntaxKind.TaggedTemplateExpression;
  readonly tag: Expression;
  readonly template: TemplateLiteral | NoSubstitutionTemplateLiteral;
}

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
  readonly name: Identifier;
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

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

export interface Parameter extends Node {
  readonly kind: SyntaxKind.Parameter;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly dotDotDotToken: boolean;
  readonly questionToken: boolean;
  readonly type?: TypeNode;
  readonly initializer?: Expression;
}

export interface TypeParameterDeclaration extends Node {
  readonly kind: SyntaxKind.TypeParameterDeclaration;
  readonly name: Identifier;
  readonly constraint?: TypeNode;
  readonly default?: TypeNode;
}

export interface FunctionDeclaration extends Node {
  readonly kind: SyntaxKind.FunctionDeclaration;
  readonly name?: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body?: Block;
  readonly flags: NodeFlags;
}

export interface HeritageClause extends Node {
  readonly kind: SyntaxKind.HeritageClause;
  readonly token: "extends" | "implements";
  readonly types: ExpressionWithTypeArguments[];
}

export interface ExpressionWithTypeArguments extends Node {
  readonly kind: SyntaxKind.ExpressionWithTypeArguments;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
}

export type ClassElement = PropertyDeclaration | MethodDeclaration | ConstructorDeclaration;

export interface PropertyDeclaration extends Node {
  readonly kind: SyntaxKind.PropertyDeclaration;
  readonly name: PropertyName;
  readonly modifiers: Modifier[];
  readonly questionToken: boolean;
  readonly exclamationToken: boolean;
  readonly type?: TypeNode;
  readonly initializer?: Expression;
}

export interface MethodDeclaration extends Node {
  readonly kind: SyntaxKind.MethodDeclaration;
  readonly name: PropertyName;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body?: Block;
  readonly flags: NodeFlags;
  readonly optional: boolean;
}

export interface ConstructorDeclaration extends Node {
  readonly kind: SyntaxKind.ConstructorDeclaration;
  readonly modifiers: Modifier[];
  readonly parameters: Parameter[];
  readonly body?: Block;
}

export interface ClassDeclaration extends Node {
  readonly kind: SyntaxKind.ClassDeclaration;
  readonly name?: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: ClassElement[];
}

export interface InterfaceDeclaration extends Node {
  readonly kind: SyntaxKind.InterfaceDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: TypeElement[];
}

export interface TypeAliasDeclaration extends Node {
  readonly kind: SyntaxKind.TypeAliasDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly type: TypeNode;
}

export interface EnumMember extends Node {
  readonly kind: SyntaxKind.EnumMember;
  readonly name: PropertyName;
  readonly initializer?: Expression;
}

export interface EnumDeclaration extends Node {
  readonly kind: SyntaxKind.EnumDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly members: EnumMember[];
}

export interface ModuleDeclaration extends Node {
  readonly kind: SyntaxKind.ModuleDeclaration;
  readonly name: Identifier | StringLiteral;
  readonly modifiers: Modifier[];
  readonly body?: Block;
}

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

export interface ImportSpecifier extends Node {
  readonly kind: SyntaxKind.ImportSpecifier;
  readonly propertyName?: Identifier;
  readonly name: Identifier;
  readonly isTypeOnly: boolean;
}

export interface NamedImports extends Node {
  readonly kind: SyntaxKind.NamedImports;
  readonly elements: ImportSpecifier[];
}

export interface NamespaceImport extends Node {
  readonly kind: SyntaxKind.NamespaceImport;
  readonly name: Identifier;
}

export interface ImportClause extends Node {
  readonly kind: SyntaxKind.ImportClause;
  readonly name?: Identifier;
  readonly namedBindings?: NamedImports | NamespaceImport;
  readonly isTypeOnly: boolean;
}

export interface ImportDeclaration extends Node {
  readonly kind: SyntaxKind.ImportDeclaration;
  readonly importClause?: ImportClause;
  readonly moduleSpecifier: StringLiteral;
  readonly attributes: { name: string; value: string }[];
}

export interface ExportSpecifier extends Node {
  readonly kind: SyntaxKind.ExportSpecifier;
  readonly propertyName?: Identifier;
  readonly name: Identifier;
  readonly isTypeOnly: boolean;
}

export interface NamedExports extends Node {
  readonly kind: SyntaxKind.NamedExports;
  readonly elements: ExportSpecifier[];
}

export interface ExportDeclaration extends Node {
  readonly kind: SyntaxKind.ExportDeclaration;
  readonly modifiers: Modifier[];
  readonly exportClause?: NamedExports | NamespaceExport;
  readonly moduleSpecifier?: StringLiteral;
  readonly isTypeOnly: boolean;
}

export interface NamespaceExport extends Node {
  readonly kind: SyntaxKind.NamespaceImport;
  readonly name: Identifier;
}

export interface ExportAssignment extends Node {
  readonly kind: SyntaxKind.ExportAssignment;
  readonly isExportEquals: boolean;
  readonly expression: Expression;
}

// ---------------------------------------------------------------------------
// Type nodes
// ---------------------------------------------------------------------------

export interface TypeReference extends Node {
  readonly kind: SyntaxKind.TypeReference;
  readonly typeName: Identifier | QualifiedName;
  readonly typeArguments: TypeNode[];
}

export interface QualifiedName extends Node {
  readonly kind: SyntaxKind.QualifiedName;
  readonly left: Identifier | QualifiedName;
  readonly right: Identifier;
}

export interface TypePredicate extends Node {
  readonly kind: SyntaxKind.TypePredicate;
  readonly parameterName: Identifier | ThisExpression;
  readonly type?: TypeNode;
}

export interface KeywordTypeNode extends Node {
  readonly kind:
    | SyntaxKind.AnyKeywordType
    | SyntaxKind.UnknownKeywordType
    | SyntaxKind.NumberKeywordType
    | SyntaxKind.BigIntKeywordType
    | SyntaxKind.StringKeywordType
    | SyntaxKind.BooleanKeywordType
    | SyntaxKind.SymbolKeywordType
    | SyntaxKind.VoidKeywordType
    | SyntaxKind.UndefinedKeywordType
    | SyntaxKind.NullKeywordType
    | SyntaxKind.NeverKeywordType
    | SyntaxKind.ObjectKeywordType
    | SyntaxKind.ThisKeywordType;
}

export interface LiteralTypeNode extends Node {
  readonly kind: SyntaxKind.LiteralType;
  readonly literal: StringLiteral | NumericLiteral | BooleanLiteral | NullLiteral;
}

export interface ArrayTypeNode extends Node {
  readonly kind: SyntaxKind.ArrayType;
  readonly elementType: TypeNode;
}

export interface TupleTypeNode extends Node {
  readonly kind: SyntaxKind.TupleType;
  readonly elements: TypeNode[];
}

export interface UnionTypeNode extends Node {
  readonly kind: SyntaxKind.UnionType;
  readonly types: TypeNode[];
}

export interface IntersectionTypeNode extends Node {
  readonly kind: SyntaxKind.IntersectionType;
  readonly types: TypeNode[];
}

export interface FunctionTypeNode extends Node {
  readonly kind: SyntaxKind.FunctionType;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType: TypeNode;
}

export type TypeElement = PropertySignature | MethodSignature | IndexSignature;

export interface PropertySignature extends Node {
  readonly kind: SyntaxKind.PropertySignature;
  readonly name: PropertyName;
  readonly questionToken: boolean;
  readonly readonlyToken: boolean;
  readonly type?: TypeNode;
}

export interface MethodSignature extends Node {
  readonly kind: SyntaxKind.MethodSignature;
  readonly name: PropertyName;
  readonly questionToken: boolean;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
}

export interface IndexSignature extends Node {
  readonly kind: SyntaxKind.IndexSignature;
  readonly parameters: Parameter[];
  readonly type?: TypeNode;
}

export interface TypeLiteralNode extends Node {
  readonly kind: SyntaxKind.TypeLiteral;
  readonly members: TypeElement[];
}

export interface ParenthesizedTypeNode extends Node {
  readonly kind: SyntaxKind.ParenthesizedType;
  readonly type: TypeNode;
}

export interface TypeQueryNode extends Node {
  readonly kind: SyntaxKind.TypeQuery;
  readonly exprName: Identifier | QualifiedName;
  readonly typeArguments: TypeNode[];
}

export interface IndexedAccessTypeNode extends Node {
  readonly kind: SyntaxKind.IndexedAccessType;
  readonly objectType: TypeNode;
  readonly indexType: TypeNode;
}

export const enum TypeOperator {
  KeyOf = "keyof",
  Unique = "unique",
  Readonly = "readonly",
}

export interface TypeOperatorNode extends Node {
  readonly kind: SyntaxKind.TypeOperator;
  readonly operator: TypeOperator;
  readonly type: TypeNode;
}

export interface MappedTypeNode extends Node {
  readonly kind: SyntaxKind.MappedType;
  readonly readonlyToken: boolean | "+" | "-";
  readonly typeParameter: TypeParameterDeclaration;
  readonly questionToken: boolean | "+" | "-";
  readonly type?: TypeNode;
}

export interface ConditionalTypeNode extends Node {
  readonly kind: SyntaxKind.ConditionalType;
  readonly checkType: TypeNode;
  readonly extendsType: TypeNode;
  readonly trueType: TypeNode;
  readonly falseType: TypeNode;
}

export interface InferTypeNode extends Node {
  readonly kind: SyntaxKind.InferType;
  readonly typeParameter: TypeParameterDeclaration;
}

export interface OptionalTypeNode extends Node {
  readonly kind: SyntaxKind.OptionalType;
  readonly type: TypeNode;
}

export interface RestTypeNode extends Node {
  readonly kind: SyntaxKind.RestType;
  readonly type: TypeNode;
}
