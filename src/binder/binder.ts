/**
 * The binder walks the AST once and answers two questions the code generator
 * and checker depend on:
 *
 *   1. Which declaration does every identifier refer to? (scopes + symbols)
 *   2. Which variables are captured by nested functions? (closure conversion)
 *
 * The result is deliberately plain data — maps and records — so it can be
 * cached, serialized for the incremental build, and unit tested in isolation.
 */

import {
  SyntaxKind,
  type ArrowFunction,
  type Block,
  type FunctionDeclaration,
  type FunctionExpression,
  type Identifier,
  type Node,
  type SourceFileNode,
  type Statement,
  type Expression,
  type VariableDeclarationList,
  type VariableStatement,
  type ImportDeclaration,
  type ExportDeclaration,
  type CatchClause,
  type ForStatement,
  type ForOfStatement,
  type ForInStatement,
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
  /** Maps a function-like AST node to the FunctionInfo the binder built for it. */
  readonly functionOfNode: Map<Node, FunctionInfo>;
  readonly symbolOfDeclaration: Map<Node, SymbolInfo>;
  readonly symbolOfIdentifier: Map<Identifier, SymbolInfo>;
  readonly scopes: Map<Node, Scope>;
  /** Identifier references that could not be resolved in user code. */
  readonly unresolved: Identifier[];
}

interface MutableFunction extends FunctionInfo {
  locals: SymbolInfo[];
  captures: SymbolInfo[];
  captureIndex: Map<number, number>;
}

export function bind(sourceFile: SourceFileNode): BindResult {
  const binder = new Binder(sourceFile);
  return binder.bind();
}

class Binder {
  private readonly functions: FunctionInfo[] = [];
  private readonly symbolOfDeclaration = new Map<Node, SymbolInfo>();
  private readonly symbolOfIdentifier = new Map<Identifier, SymbolInfo>();
  private readonly scopes = new Map<Node, Scope>();
  private readonly unresolved: Identifier[] = [];
  private nextSymbolId = 1;
  private nextFunctionId = 1;
  private current!: MutableFunction;

  constructor(private readonly sourceFile: SourceFileNode) {}

  bind(): BindResult {
    this.current = this.createFunction("(module)", this.sourceFile, undefined, true, false);
    const scope = this.createScope(ScopeKind.Module, this.sourceFile, undefined);
    (this.current as { scope: Scope }).scope = scope;
    this.scopes.set(this.sourceFile, scope);

    this.predeclareStatements(this.sourceFile.statements, scope);
    this.collectFunctionScoped(this.sourceFile.statements, this.current);
    this.bindStatements(this.sourceFile.statements, scope);

    return {
      moduleFunction: this.current,
      functions: this.functions,
      functionOfNode: new Map(this.functions.map((fn) => [fn.node as Node, fn])),
      symbolOfDeclaration: this.symbolOfDeclaration,
      symbolOfIdentifier: this.symbolOfIdentifier,
      scopes: this.scopes,
      unresolved: this.unresolved,
    };
  }

  // -- construction helpers ------------------------------------------------

  private createFunction(
    name: string,
    node: FunctionNode,
    parent: MutableFunction | undefined,
    isModule: boolean,
    isArrow: boolean,
  ): MutableFunction {
    const fn: MutableFunction = {
      id: this.nextFunctionId++,
      name,
      node,
      parent,
      scope: undefined as unknown as Scope,
      params: [],
      locals: [],
      captures: [],
      captureIndex: new Map(),
      isModule,
      isArrow,
    };
    this.functions.push(fn);
    return fn;
  }

  private createScope(kind: ScopeKind, node: Node | undefined, parent: Scope | undefined): Scope {
    return { kind, node, parent, fn: this.current, symbols: new Map() };
  }

  private declare(
    name: string,
    kind: SymbolKind,
    node: Node,
    scope: Scope,
    mutable: boolean,
  ): SymbolInfo {
    const existing = scope.symbols.get(name);
    if (existing) {
      // `var` redeclarations and duplicate functions merge into one symbol.
      if (
        (kind === SymbolKind.Var && existing.kind === SymbolKind.Var) ||
        (kind === SymbolKind.Function && existing.kind === SymbolKind.Function)
      ) {
        (existing.declarations as Node[]).push(node);
      }
      this.symbolOfDeclaration.set(node, existing);
      return existing;
    }
    const symbol: SymbolInfo = {
      id: this.nextSymbolId++,
      name,
      kind,
      mutable,
      fn: this.current,
      declarations: [node],
      references: [],
      captured: false,
      boxed: false,
    };
    scope.symbols.set(name, symbol);
    this.symbolOfDeclaration.set(node, symbol);
    if (scope.fn === this.current) this.current.locals.push(symbol);
    return symbol;
  }

  // -- declaration passes --------------------------------------------------

  /** Pre-declare block-scoped bindings that appear directly in `statements`. */
  private predeclareStatements(statements: readonly Statement[], scope: Scope): void {
    for (const statement of statements) {
      switch (statement.kind) {
        case SyntaxKind.VariableStatement: {
          this.predeclareVariableList((statement as VariableStatement).declarationList, scope);
          break;
        }
        case SyntaxKind.FunctionDeclaration:
          this.declare((statement as FunctionDeclaration).name?.text ?? "(anonymous)", SymbolKind.Function, statement, scope, true);
          break;
        case SyntaxKind.ClassDeclaration:
          this.declare((statement as { name: Identifier }).name.text, SymbolKind.Class, statement, scope, false);
          break;
        case SyntaxKind.InterfaceDeclaration:
          this.declare((statement as { name: Identifier }).name.text, SymbolKind.Interface, statement, scope, false);
          break;
        case SyntaxKind.TypeAliasDeclaration:
          this.declare((statement as { name: Identifier }).name.text, SymbolKind.TypeAlias, statement, scope, false);
          break;
        case SyntaxKind.EnumDeclaration:
          this.declare((statement as { name: Identifier }).name.text, SymbolKind.Enum, statement, scope, true);
          break;
        case SyntaxKind.ImportDeclaration:
          this.predeclareImport(statement as ImportDeclaration, scope);
          break;
        case SyntaxKind.ExportDeclaration: {
          const exported = statement as ExportDeclaration;
          if (exported.exportClause && exported.exportClause.kind === SyntaxKind.NamedExports) {
            for (const specifier of exported.exportClause.elements) {
              if (specifier.propertyName) this.reference(specifier.propertyName, scope);
            }
          }
          break;
        }
        case SyntaxKind.ModuleDeclaration:
          this.declare((statement as { name: Identifier }).name.text, SymbolKind.Namespace, statement, scope, false);
          break;
        default:
          break;
      }
    }
  }

  private predeclareVariableList(list: VariableDeclarationList, scope: Scope): void {
    const kind =
      list.declarationKind === "const"
        ? SymbolKind.Const
        : list.declarationKind === "var"
          ? SymbolKind.Var
          : SymbolKind.Let;
    for (const declaration of list.declarations) {
      this.declare(declaration.name.text, kind, declaration, scope, kind !== SymbolKind.Const);
    }
  }

  private predeclareImport(node: ImportDeclaration, scope: Scope): void {
    const clause = node.importClause;
    if (!clause) return;
    if (clause.name) this.declare(clause.name.text, SymbolKind.Import, clause.name, scope, false);
    const bindings = clause.namedBindings;
    if (bindings && bindings.kind === SyntaxKind.NamedImports) {
      for (const specifier of bindings.elements) {
        this.declare(specifier.name.text, SymbolKind.Import, specifier.name, scope, false);
      }
    } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
      this.declare(bindings.name.text, SymbolKind.Import, bindings.name, scope, false);
    }
  }

  /**
   * Collect `var` and function declarations from nested blocks into the
   * enclosing function scope, without crossing function boundaries.
   */
  private collectFunctionScoped(statements: readonly Statement[], fn: MutableFunction): void {
    const visit = (node: Node): void => {
      switch (node.kind) {
        case SyntaxKind.FunctionDeclaration:
        case SyntaxKind.FunctionExpression:
        case SyntaxKind.ArrowFunction:
        case SyntaxKind.ClassDeclaration:
        case SyntaxKind.ClassExpression:
          return; // opaque, except we still want the declaration name handled at its site
        case SyntaxKind.VariableStatement: {
          const decl = node as VariableStatement;
          if (decl.declarationList.declarationKind === "var") {
            for (const d of decl.declarationList.declarations) {
              const existing = fn.scope.symbols.get(d.name.text);
              if (!existing) this.declare(d.name.text, SymbolKind.Var, d, fn.scope, true);
              else this.symbolOfDeclaration.set(d, existing);
            }
          }
          return;
        }
        default:
          break;
      }
      for (const child of childNodes(node)) visit(child);
    };
    for (const statement of statements) {
      if (
        statement.kind === SyntaxKind.FunctionDeclaration ||
        statement.kind === SyntaxKind.ClassDeclaration
      ) {
        const name = (statement as { name: Identifier }).name.text;
        if (!fn.scope.symbols.has(name)) {
          this.declare(name, statement.kind === SyntaxKind.FunctionDeclaration ? SymbolKind.Function : SymbolKind.Class, statement, fn.scope, statement.kind === SyntaxKind.FunctionDeclaration);
        }
        continue;
      }
      visit(statement);
    }
  }

  // -- reference pass ------------------------------------------------------

  private bindStatements(statements: readonly Statement[], scope: Scope): void {
    for (const statement of statements) this.bindNode(statement, scope);
  }

  private bindBlock(block: Block, scope: Scope): void {
    const blockScope = this.createScope(ScopeKind.Block, block, scope);
    this.scopes.set(block, blockScope);
    this.predeclareStatements(block.statements, blockScope);
    this.bindStatements(block.statements, blockScope);
  }

  private bindNode(node: Node | undefined, scope: Scope): void {
    if (!node) return;
    switch (node.kind) {
      case SyntaxKind.Block:
        this.bindBlock(node as Block, scope);
        return;
      case SyntaxKind.VariableStatement:
        this.bindVariableList((node as VariableStatement).declarationList, scope);
        return;
      case SyntaxKind.FunctionDeclaration: {
        const fn = node as FunctionDeclaration;
        const symbol = this.symbolOfDeclaration.get(fn);
        if (symbol) (symbol.declarations as Node[]).length; // keep declaration mapping warm
        this.bindFunction(fn, scope, symbol);
        return;
      }
      case SyntaxKind.FunctionExpression:
      case SyntaxKind.ArrowFunction:
        this.bindFunction(node as FunctionExpression | ArrowFunction, scope, undefined);
        return;
      case SyntaxKind.Identifier:
        this.reference(node as Identifier, scope);
        return;
      case SyntaxKind.InterfaceDeclaration:
      case SyntaxKind.TypeAliasDeclaration:
        // Type-only: skip the body entirely.
        return;
      case SyntaxKind.ImportDeclaration:
        return;
      case SyntaxKind.ExportDeclaration: {
        const exported = node as ExportDeclaration;
        if (exported.exportClause && exported.exportClause.kind === SyntaxKind.NamedExports) {
          for (const specifier of exported.exportClause.elements) {
            if (specifier.propertyName) this.reference(specifier.propertyName, scope);
          }
        }
        return;
      }
      case SyntaxKind.ExportAssignment:
        this.bindNode((node as unknown as { expression: Expression }).expression, scope);
        return;
      case SyntaxKind.ForStatement: {
        const forNode = node as ForStatement;
        const forScope = this.createScope(ScopeKind.For, forNode, scope);
        this.scopes.set(forNode, forScope);
        if (forNode.initializer) {
          if (forNode.initializer.kind === SyntaxKind.VariableDeclarationList) {
            this.predeclareVariableList(forNode.initializer as VariableDeclarationList, forScope);
            this.bindVariableList(forNode.initializer as VariableDeclarationList, forScope);
          } else {
            this.bindNode(forNode.initializer, forScope);
          }
        }
        this.bindNode(forNode.condition, forScope);
        this.bindNode(forNode.incrementor, forScope);
        this.bindNode(forNode.statement, forScope);
        return;
      }
      case SyntaxKind.ForOfStatement:
      case SyntaxKind.ForInStatement: {
        const loop = node as ForOfStatement | ForInStatement;
        const loopScope = this.createScope(ScopeKind.For, loop, scope);
        this.scopes.set(loop, loopScope);
        if (loop.initializer.kind === SyntaxKind.VariableDeclarationList) {
          this.predeclareVariableList(loop.initializer, loopScope);
          this.bindVariableList(loop.initializer, loopScope);
        } else {
          this.bindNode(loop.initializer, loopScope);
        }
        this.bindNode(loop.expression, loopScope);
        this.bindNode(loop.statement, loopScope);
        return;
      }
      case SyntaxKind.CatchClause: {
        const clause = node as CatchClause;
        const catchScope = this.createScope(ScopeKind.Catch, clause, scope);
        this.scopes.set(clause, catchScope);
        if (clause.variable) this.declare(clause.variable.text, SymbolKind.Let, clause.variable, catchScope, true);
        this.bindBlock(clause.block, catchScope);
        return;
      }
      default:
        break;
    }
    for (const child of childNodes(node)) this.bindNode(child, scope);
  }

  private bindVariableList(list: VariableDeclarationList, scope: Scope): void {
    for (const declaration of list.declarations) {
      const symbol = this.symbolOfDeclaration.get(declaration);
      if (declaration.name) this.symbolOfDeclaration.set(declaration.name, symbol ?? this.symbolOfDeclaration.get(declaration) as SymbolInfo);
      this.bindNode(declaration.initializer, scope);
    }
  }

  private bindFunction(
    node: FunctionDeclaration | FunctionExpression | ArrowFunction,
    parentScope: Scope,
    symbol: SymbolInfo | undefined,
  ): void {
    const name =
      node.kind === SyntaxKind.FunctionDeclaration
        ? (node as FunctionDeclaration).name?.text ?? "(anonymous)"
        : node.kind === SyntaxKind.FunctionExpression
          ? (node as FunctionExpression).name?.text ?? "(anonymous)"
          : "(arrow)";
    const parentFn = this.current;
    const fn = this.createFunction(name, node, parentFn, false, node.kind === SyntaxKind.ArrowFunction);
    this.current = fn;

    const scope = this.createScope(ScopeKind.Function, node, parentScope);
    (fn as { scope: Scope }).scope = scope;
    this.scopes.set(node, scope);

    // Parameters live in the function scope.
    const params: SymbolInfo[] = [];
    for (const parameter of node.parameters) {
      const paramSymbol = this.declare(parameter.name.text, SymbolKind.Parameter, parameter, scope, true);
      params.push(paramSymbol);
      if (parameter.initializer) this.bindNode(parameter.initializer, scope);
    }
    (fn as { params: SymbolInfo[] }).params = params;

    // Initialize a named function expression's own name inside its scope.
    if (node.kind === SyntaxKind.FunctionExpression && (node as FunctionExpression).name) {
      const selfName = (node as FunctionExpression).name!;
      this.declare(selfName.text, SymbolKind.Const, selfName, scope, false);
    }
    if (symbol) this.symbolOfDeclaration.set(node, symbol);

    const body = node.body as unknown as Block | Expression | undefined;
    if (body) {
      this.collectFunctionScoped([body.kind === SyntaxKind.Block ? body : ({ kind: SyntaxKind.Block, statements: [], start: body.start, end: body.end } as Block)], fn);
      if (body.kind === SyntaxKind.Block) {
        this.predeclareStatements(body.statements, scope);
        this.bindStatements(body.statements, scope);
      } else {
        this.bindNode(body, scope);
      }
    }

    this.current = parentFn;
  }

  private reference(identifier: Identifier, scope: Scope): void {
    let current: Scope | undefined = scope;
    while (current) {
      const symbol = current.symbols.get(identifier.text);
      if (symbol) {
        (symbol.references as Identifier[]).push(identifier);
        this.symbolOfIdentifier.set(identifier, symbol);
        // Functions, classes and type-only names are resolved to their
        // declaration sites by codegen; they are never captured as values.
        if (symbol.fn !== this.current && isCapturable(symbol.kind)) this.markCaptured(symbol);
        return;
      }
      current = current.parent;
    }
    this.unresolved.push(identifier);
  }

  /** Record that `symbol` escapes into `this.current`, threading it through every intermediate closure. */
  private markCaptured(symbol: SymbolInfo): void {
    symbol.captured = true;
    symbol.boxed = true;
    let fn: MutableFunction | undefined = this.current;
    while (fn && fn !== symbol.fn) {
      if (!fn.captureIndex.has(symbol.id)) {
        fn.captureIndex.set(symbol.id, fn.captures.length);
        fn.captures.push(symbol);
      }
      fn = fn.parent as MutableFunction | undefined;
    }
  }
}

/** Direct AST children, tolerant of both arrays and single nodes. */
function childNodes(node: Node): Node[] {
  const result: Node[] = [];
  const n = node as unknown as Record<string, unknown>;
  for (const key of Object.keys(n)) {
    if (key === "kind" || key === "start" || key === "end") continue;
    const value = n[key];
    if (Array.isArray(value)) {
      for (const item of value) if (item && typeof item === "object" && "kind" in (item as object)) result.push(item as Node);
    } else if (value && typeof value === "object" && "kind" in (value as object)) {
      result.push(value as Node);
    }
  }
  return result;
}

/** Only value bindings participate in closure capture. */
function isCapturable(kind: SymbolKind): boolean {
  return (
    kind === SymbolKind.Var ||
    kind === SymbolKind.Let ||
    kind === SymbolKind.Const ||
    kind === SymbolKind.Parameter
  );
}
