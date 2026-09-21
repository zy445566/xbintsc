/**
 * The binder walks the AST once and answers two questions the code generator
 * and checker depend on:
 *
 *   1. Which declaration does every identifier refer to? (scopes + symbols)
 *   2. Which variables are captured by nested functions? (closure conversion)
 *
 * The result is deliberately plain data — maps and records — so it can be
 * cached, serialized for the incremental build, and unit tested in isolation.
 *
 * The walk is split by functional area into sibling modules:
 *   - `types`        — the plain-data symbol/scope model
 *   - `declarations` — symbol/scope creation and hoisting
 *   - `references`   — identifier resolution and capture analysis
 *   - `helpers`      — small AST utilities
 */

import { NodeFlags } from "../ast/kinds.js";
import type { Identifier, Node, SourceFileNode } from "../ast/nodes.js";
import {
  ScopeKind,
  type ClassInfo,
  type FunctionInfo,
  type MutableFunction,
  type Scope,
  type SymbolInfo,
  type BindResult,
  type FunctionNode,
} from "./types.js";
import { declarationMethods, type DeclarationMethods } from "./declarations.js";
import { referenceMethods, type ReferenceMethods } from "./references.js";

export * from "./types.js";

export function bind(sourceFile: SourceFileNode): BindResult {
  const binder = new Binder(sourceFile);
  return binder.bind();
}

export class Binder {
  readonly functions: FunctionInfo[] = [];
  readonly classes: ClassInfo[] = [];
  readonly classOfNode = new Map<Node, ClassInfo>();
  readonly symbolOfDeclaration = new Map<Node, SymbolInfo>();
  readonly symbolOfIdentifier = new Map<Identifier, SymbolInfo>();
  readonly scopes = new Map<Node, Scope>();
  readonly unresolved: Identifier[] = [];
  nextSymbolId = 1;
  nextFunctionId = 1;
  nextClassId = 1;
  current!: MutableFunction;

  constructor(readonly sourceFile: SourceFileNode) {}

  bind(): BindResult {
    this.current = this.createFunction("(module)", this.sourceFile, undefined, true, false);
    const scope = this.createScope(ScopeKind.Module, this.sourceFile, undefined);
    (this.current as { scope: Scope }).scope = scope;
    this.scopes.set(this.sourceFile, scope);

    this.predeclareStatements(this.sourceFile.statements, scope);
    this.collectFunctionScoped(this.sourceFile.statements, this.current);
    this.bindStatements(this.sourceFile.statements, scope);

    // Arrows inherit `this` lexically: propagate the need to capture it up the
    // chain of arrow functions until reaching a regular function.
    for (let index = this.functions.length - 1; index >= 0; index--) {
      const fn = this.functions[index]! as MutableFunction;
      if (fn.usesThis && fn.isArrow) {
        fn.capturesThis = true;
        const parent = fn.parent as MutableFunction | undefined;
        if (parent && parent.isArrow) parent.usesThis = true;
      }
    }

    return {
      moduleFunction: this.current,
      functions: this.functions,
      classes: this.classes,
      classOfNode: this.classOfNode,
      functionOfNode: new Map(this.functions.map((fn) => [fn.node as Node, fn])),
      symbolOfDeclaration: this.symbolOfDeclaration,
      symbolOfIdentifier: this.symbolOfIdentifier,
      scopes: this.scopes,
      unresolved: this.unresolved,
    };
  }

  // -- construction helpers ------------------------------------------------

  createFunction(
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
      isAsync: (((node as { flags?: number }).flags ?? 0) & NodeFlags.Async) !== 0,
      usesThis: false,
      capturesThis: false,
    };
    this.functions.push(fn);
    return fn;
  }

  createScope(kind: ScopeKind, node: Node | undefined, parent: Scope | undefined): Scope {
    return { kind, node, parent, fn: this.current, symbols: new Map() };
  }
}

// eslint-disable-next-line @typescript-eslint/no-empty-interface
export interface Binder extends DeclarationMethods, ReferenceMethods {}

Object.assign(Binder.prototype, declarationMethods, referenceMethods);
