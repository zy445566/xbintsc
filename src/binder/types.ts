/**
 * Binder data model: the plain-data symbols, scopes and per-function records
 * that the binder produces and the checker/codegen consume.
 */

import type {
  ArrowFunction,
  Expression,
  FunctionDeclaration,
  FunctionExpression,
  Identifier,
  Node,
  SourceFileNode,
} from "../ast/nodes.js";

export enum SymbolKind {
  Var = "var",
  Let = "let",
  Const = "const",
  Function = "function",
  Parameter = "parameter",
  Class = "class",
  Interface = "interface",
  TypeAlias = "type",
  Enum = "enum",
  Import = "import",
  Namespace = "namespace",
}

export interface SymbolInfo {
  readonly id: number;
  readonly name: string;
  readonly kind: SymbolKind;
  /** False for `const` bindings and imports. */
  readonly mutable: boolean;
  /** The function this symbol belongs to. */
  readonly fn: FunctionInfo;
  readonly declarations: Node[];
  readonly references: Identifier[];
  /** Set when referenced from a nested function. */
  captured: boolean;
  /** Assigned after construction once we know it is captured. */
  boxed: boolean;
}

/**
 * A same-scope declaration that cannot legally merge with the symbol already
 * bound to that name (`let x` twice, `function f` then `const f`, ...).
 *
 * The binder records the conflict instead of reporting it: whether it is an
 * error is checker policy (`--no-check` keeps the old, permissive behaviour),
 * while the fact itself is cheap to capture during the one declaration pass.
 */
export interface DeclarationConflict {
  readonly name: string;
  /** The symbol the name was already bound to. */
  readonly existing: SymbolInfo;
  /** The kind of the offending declaration. */
  readonly kind: SymbolKind;
  /** The offending declaration node (the second one to use the name). */
  readonly node: Node;
}

export type FunctionNode = SourceFileNode | FunctionDeclaration | FunctionExpression | ArrowFunction;

export interface FunctionInfo {
  readonly id: number;
  readonly name: string;
  readonly node: FunctionNode;
  readonly parent?: FunctionInfo;
  readonly scope: Scope;
  readonly params: SymbolInfo[];
  /** Every binding declared directly in this function (params + locals). */
  readonly locals: SymbolInfo[];
  /** Outer bindings referenced by this function or any descendant. */
  readonly captures: SymbolInfo[];
  readonly captureIndex: Map<number, number>;
  readonly isModule: boolean;
  readonly isArrow: boolean;
  /** True when the function was declared `async`. */
  readonly isAsync: boolean;
  /** True for `function*` / generator methods; calling one creates a generator. */
  readonly isGenerator: boolean;
  /** True when this function's body mentions `this` directly. */
  usesThis: boolean;
  /** True when an arrow function must receive `this` from its enclosing scope. */
  capturesThis: boolean;
  /** Set for class methods/constructors; carries the owning class. */
  classInfo?: ClassInfo;
  /** Computed member name (`[expr]() {}`), evaluated at class definition. */
  computedKey?: Expression;
  /** True for `static` class members. */
  isStatic?: boolean;
  /** True for an explicit `constructor`. */
  isConstructor?: boolean;
}

export interface ClassInfo {
  readonly id: number;
  readonly name: string;
  readonly node: Node;
  readonly scope: Scope;
  readonly parent?: ClassInfo;
  readonly parentExpression?: Expression;
  /** `this`-bearing fields declared on the class. */
  readonly fields: ClassFieldInfo[];
  readonly methods: FunctionInfo[];
  readonly statics: FunctionInfo[];
  /** `static { ... }` blocks, in declaration order. */
  readonly staticBlocks: FunctionInfo[];
  /** Static fields and static blocks, interleaved in declaration order: they
   *  run in source order, while static methods are installed before both. */
  readonly staticInit: ClassStaticInit[];
  ctor?: FunctionInfo;
  /** The symbol the class name binds to, when there is one. */
}

/** One static element evaluated at class-definition time, in source order. */
export type ClassStaticInit =
  | { readonly kind: "field"; readonly field: ClassFieldInfo }
  | { readonly kind: "block"; readonly fn: FunctionInfo };

export interface ClassFieldInfo {
  readonly name: string;
  readonly initializer: Expression | undefined;
  readonly fn: FunctionInfo;
  readonly isStatic: boolean;
}

export enum ScopeKind {
  Module = "module",
  Function = "function",
  Block = "block",
  For = "for",
  Catch = "catch",
}

export interface Scope {
  readonly kind: ScopeKind;
  readonly parent?: Scope;
  readonly fn: FunctionInfo;
  readonly symbols: Map<string, SymbolInfo>;
  /** The syntax node that introduced the scope, when there is one. */
  readonly node?: Node;
}

export interface BindResult {
  readonly moduleFunction: FunctionInfo;
  readonly functions: FunctionInfo[];
  readonly classes: ClassInfo[];
  /** Maps a class-like AST node to the ClassInfo the binder built. */
  readonly classOfNode: Map<Node, ClassInfo>;
  /** Maps a function-like AST node to the FunctionInfo the binder built for it. */
  readonly functionOfNode: Map<Node, FunctionInfo>;
  readonly symbolOfDeclaration: Map<Node, SymbolInfo>;
  readonly symbolOfIdentifier: Map<Identifier, SymbolInfo>;
  readonly scopes: Map<Node, Scope>;
  /** Identifier references that could not be resolved in user code. */
  readonly unresolved: Identifier[];
  /** Same-scope declarations that cannot merge (see {@link DeclarationConflict}). */
  readonly conflicts: readonly DeclarationConflict[];
}

export interface MutableFunction extends FunctionInfo {
  locals: SymbolInfo[];
  captures: SymbolInfo[];
  captureIndex: Map<number, number>;
}
