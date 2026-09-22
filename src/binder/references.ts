/**
 * Reference pass: walks statements/expressions, resolves identifier references
 * to symbols, and records closure captures.
 */

import { ModifierKind } from "../ast/kinds.js";
import {
  SyntaxKind,
  type ArrowFunction,
  type BindingName,
  type Block,
  type CatchClause,
  type ClassDeclaration,
  type ClassExpression,
  type ConstructorDeclaration,
  type ExportDeclaration,
  type Expression,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type FunctionDeclaration,
  type FunctionExpression,
  type Identifier,
  type MethodDeclaration,
  type Node,
  type PropertyDeclaration,
  type PropertyName,
  type Statement,
  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";
import type { ArrayBindingPattern, ObjectBindingPattern, Parameter } from "../ast/declarations.js";
import {
  ScopeKind,
  SymbolKind,
  type ClassInfo,
  type FunctionInfo,
  type FunctionNode,
  type MutableFunction,
  type Scope,
  type SymbolInfo,
} from "./types.js";
import { childNodes, classMemberName, isCapturable } from "./helpers.js";
import type { Binder } from "./binder.js";

export interface ReferenceMethods {
  bindStatements(this: Binder, statements: readonly Statement[], scope: Scope): void;
  bindBlock(this: Binder, block: Block, scope: Scope): void;
  bindNode(this: Binder, node: Node | undefined, scope: Scope): void;
  bindVariableList(this: Binder, list: VariableDeclarationList, scope: Scope): void;
  bindBindingPattern(this: Binder, name: BindingName, scope: Scope): void;
  bindFunction(this: Binder, node: FunctionDeclaration | FunctionExpression | ArrowFunction, parentScope: Scope, symbol: SymbolInfo | undefined, inferredName?: string): void;
  bindClass(this: Binder, node: ClassDeclaration | ClassExpression, scope: Scope): void;
  createClassFunction(this: Binder, member: Node, name: string, parameters: readonly Parameter[], body: Block | undefined, parentScope: Scope, info: ClassInfo, isStatic: boolean, isConstructor: boolean): FunctionInfo;
  reference(this: Binder, identifier: Identifier, scope: Scope): void;
  markCaptured(this: Binder, symbol: SymbolInfo): void;
}

export const referenceMethods: ReferenceMethods = {
  bindStatements(statements: readonly Statement[], scope: Scope): void {
    for (const statement of statements) this.bindNode(statement, scope);
  },

  bindBlock(block: Block, scope: Scope): void {
    const blockScope = this.createScope(ScopeKind.Block, block, scope);
    this.scopes.set(block, blockScope);
    this.predeclareStatements(block.statements, blockScope);
    this.bindStatements(block.statements, blockScope);
  },

  bindNode(node: Node | undefined, scope: Scope): void {
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
      case SyntaxKind.PropertyAccessExpression:
        // `obj.name` reads a property, not a variable named `name`.
        this.bindNode((node as unknown as { expression: Expression }).expression, scope);
        return;
      case SyntaxKind.ElementAccessExpression: {
        const access = node as unknown as { expression: Expression; argumentExpression: Expression };
        this.bindNode(access.expression, scope);
        this.bindNode(access.argumentExpression, scope);
        return;
      }
      case SyntaxKind.PropertyAssignment: {
        // `{ key: value }` / `{ key() {} }`: only the value is an expression,
        // except a computed key (`{ [expr]: value }`) whose expression is real.
        const property = node as unknown as { name: PropertyName; initializer: Expression };
        if (property.name.kind === SyntaxKind.ComputedPropertyName) {
          this.bindNode((property.name as unknown as { expression: Expression }).expression, scope);
        }
        const initializer = property.initializer;
        if (
          initializer.kind === SyntaxKind.FunctionExpression ||
          initializer.kind === SyntaxKind.ArrowFunction
        ) {
          // Infer the function name from the property key (`{ sum() {} }`).
          const accessor = (property as unknown as { accessor?: "get" | "set" }).accessor;
          const key = classMemberName(property.name);
          const inferred = accessor ? `${accessor} ${key}` : key;
          this.bindFunction(
            initializer as FunctionExpression | ArrowFunction,
            scope,
            undefined,
            inferred,
          );
        } else {
          this.bindNode(initializer, scope);
        }
        return;
      }
      case SyntaxKind.ShorthandPropertyAssignment: {
        const shorthand = node as unknown as { name: Identifier; initializer?: Expression };
        this.reference(shorthand.name, scope);
        if (shorthand.initializer) this.bindNode(shorthand.initializer, scope);
        return;
      }
      case SyntaxKind.LabeledStatement:
        this.bindNode((node as unknown as { statement: Node }).statement, scope);
        return;
      case SyntaxKind.BreakStatement:
      case SyntaxKind.ContinueStatement:
        return;
      case SyntaxKind.ThisKeyword:
        this.current.usesThis = true;
        return;
      case SyntaxKind.ClassDeclaration:
      case SyntaxKind.ClassExpression:
        this.bindClass(node as ClassDeclaration | ClassExpression, scope);
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
  },

  bindVariableList(list: VariableDeclarationList, scope: Scope): void {
    for (const declaration of list.declarations) {
      const symbol = this.symbolOfDeclaration.get(declaration);
      if (declaration.name.kind === SyntaxKind.Identifier) {
        this.symbolOfDeclaration.set(declaration.name, symbol ?? this.symbolOfDeclaration.get(declaration) as SymbolInfo);
      } else {
        this.bindBindingPattern(declaration.name, scope);
      }
      const initializer = declaration.initializer;
      if (
        declaration.name.kind === SyntaxKind.Identifier &&
        initializer &&
        (initializer.kind === SyntaxKind.FunctionExpression ||
          initializer.kind === SyntaxKind.ArrowFunction)
      ) {
        // Infer the function name from the variable it is assigned to.
        this.bindFunction(
          initializer as FunctionExpression | ArrowFunction,
          scope,
          undefined,
          (declaration.name as Identifier).text,
        );
      } else {
        this.bindNode(initializer, scope);
      }
    }
  },

  /** Bind default-value expressions inside a binding pattern. */
  bindBindingPattern(name: BindingName, scope: Scope): void {
    if (name.kind === SyntaxKind.Identifier) return;
    const pattern = name as ArrayBindingPattern | ObjectBindingPattern;
    for (const element of pattern.elements) {
      if (!element) continue;
      if (element.initializer) this.bindNode(element.initializer, scope);
      this.bindBindingPattern(element.name, scope);
    }
  },

  bindFunction(
    node: FunctionDeclaration | FunctionExpression | ArrowFunction,
    parentScope: Scope,
    symbol: SymbolInfo | undefined,
    inferredName?: string,
  ): void {
    const name =
      node.kind === SyntaxKind.FunctionDeclaration
        ? (node as FunctionDeclaration).name?.text ?? "(anonymous)"
        : node.kind === SyntaxKind.FunctionExpression
          ? (node as FunctionExpression).name?.text ?? "(anonymous)"
          : "(arrow)";
    const parentFn = this.current;
    const fn = this.createFunction(name, node, parentFn, false, node.kind === SyntaxKind.ArrowFunction);
    if (inferredName && (name === "(anonymous)" || name === "(arrow)")) {
      (fn as { name: string }).name = inferredName;
    }
    this.current = fn;

    const scope = this.createScope(ScopeKind.Function, node, parentScope);
    (fn as { scope: Scope }).scope = scope;
    this.scopes.set(node, scope);

    // Parameters live in the function scope.
    const params: SymbolInfo[] = [];
    for (const parameter of node.parameters) {
      const paramSymbol = this.declareBinding(parameter.name, SymbolKind.Parameter, parameter, scope, true);
      if (paramSymbol) params.push(paramSymbol);
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
  },

  bindClass(node: ClassDeclaration | ClassExpression, scope: Scope): void {
    const parentFn = this.current;
    const name = node.name ? node.name.text : "(anonymous)";

    let parentExpression: Expression | undefined;
    let parentClass: ClassInfo | undefined;
    const heritage = node.heritage.find((clause) => clause.token === "extends");
    if (heritage && heritage.types.length > 0) {
      parentExpression = heritage.types[0]!.expression;
      this.bindNode(parentExpression, scope);
      if (parentExpression.kind === SyntaxKind.Identifier) {
        const symbol = this.symbolOfIdentifier.get(parentExpression as Identifier);
        const declaration = symbol?.declarations[0];
        if (declaration) parentClass = this.classOfNode.get(declaration);
      }
    }

    const info: ClassInfo = {
      id: this.nextClassId++,
      name,
      node,
      scope,
      parent: parentClass,
      parentExpression,
      fields: [],
      methods: [],
      statics: [],
    };
    this.classes.push(info);
    this.classOfNode.set(node, info);

    const classScope = this.createScope(ScopeKind.Block, node, scope);
    this.scopes.set(node, classScope);

    let constructor: FunctionInfo | undefined;
    for (const member of node.members) {
      if (member.kind === SyntaxKind.ConstructorDeclaration) {
        const ctor = member as ConstructorDeclaration;
        constructor = this.createClassFunction(ctor, "constructor", ctor.parameters, ctor.body, classScope, info, false, true);
      } else if (member.kind === SyntaxKind.MethodDeclaration) {
        const method = member as MethodDeclaration;
        const isStatic = method.modifiers.some((modifier) => modifier.modifierKind === ModifierKind.Static);
        const fn = this.createClassFunction(method, classMemberName(method.name), method.parameters, method.body, classScope, info, isStatic, false);
        if (method.name.kind === SyntaxKind.ComputedPropertyName) {
          // Computed member names are evaluated in the enclosing scope when
          // the class is defined, so bind them there.
          const keyExpression = (method.name as unknown as { expression: Expression }).expression;
          this.bindNode(keyExpression, scope);
          (fn as MutableFunction & { computedKey: Expression }).computedKey = keyExpression;
        }
        if (isStatic) (info.statics as FunctionInfo[]).push(fn);
        else (info.methods as FunctionInfo[]).push(fn);
      }
    }
    if (!constructor) {
      constructor = this.createClassFunction(node, "constructor", [], undefined, classScope, info, false, true);
    }
    info.ctor = constructor;

    // Field initializers run in the constructor's context so they can capture
    // enclosing variables.
    for (const member of node.members) {
      if (member.kind === SyntaxKind.PropertyDeclaration) {
        const property = member as PropertyDeclaration;
        const isStatic = property.modifiers.some((modifier) => modifier.modifierKind === ModifierKind.Static);
        const owner = this.current;
        this.current = constructor as MutableFunction;
        if (property.initializer) this.bindNode(property.initializer, classScope);
        this.current = owner;
        info.fields.push({ name: classMemberName(property.name), initializer: property.initializer, fn: constructor, isStatic });
      }
    }

    this.current = parentFn;
  },

  createClassFunction(
    member: Node,
    name: string,
    parameters: readonly Parameter[],
    body: Block | undefined,
    parentScope: Scope,
    info: ClassInfo,
    isStatic: boolean,
    isConstructor: boolean,
  ): FunctionInfo {
    const parentFn = this.current;
    const fn = this.createFunction(name, member as unknown as FunctionNode, parentFn, false, false);
    (fn as MutableFunction & { classInfo: ClassInfo }).classInfo = info;
    (fn as MutableFunction & { isStatic: boolean }).isStatic = isStatic;
    (fn as MutableFunction & { isConstructor: boolean }).isConstructor = isConstructor;
    this.current = fn as MutableFunction;
    const scope = this.createScope(ScopeKind.Function, member, parentScope);
    (fn as { scope: Scope }).scope = scope;
    this.scopes.set(member, scope);

    const params: SymbolInfo[] = [];
    for (const parameter of parameters) {
      const paramSymbol = this.declareBinding(parameter.name, SymbolKind.Parameter, parameter, scope, true);
      if (paramSymbol) params.push(paramSymbol);
      if (parameter.initializer) this.bindNode(parameter.initializer, scope);
    }
    (fn as { params: SymbolInfo[] }).params = params;

    if (body) {
      this.collectFunctionScoped([body], fn as MutableFunction);
      this.predeclareStatements(body.statements, scope);
      this.bindStatements(body.statements, scope);
    }

    this.current = parentFn;
    return fn;
  },

  reference(identifier: Identifier, scope: Scope): void {
    if (identifier.text === "super") {
      this.current.usesThis = true;
      return;
    }
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
  },

  /** Record that `symbol` escapes into `this.current`, threading it through every intermediate closure. */
  markCaptured(symbol: SymbolInfo): void {
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
  },
};
