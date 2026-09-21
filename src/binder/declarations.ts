/**
 * Declaration pass: creates symbols/scopes and pre-declares the bindings that
 * are visible before their textual position (`var`, functions, hoisted blocks).
 */

import {
  SyntaxKind,
  type BindingName,
  type ExportDeclaration,
  type FunctionDeclaration,
  type Identifier,
  type ImportDeclaration,
  type Node,
  type Statement,
  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";
import type { ArrayBindingPattern, ObjectBindingPattern } from "../ast/declarations.js";
import { SymbolKind, type MutableFunction, type Scope, type SymbolInfo } from "./types.js";
import { childNodes } from "./helpers.js";
import type { Binder } from "./binder.js";

export interface DeclarationMethods {
  declareBinding(this: Binder, name: BindingName, kind: SymbolKind, declaration: Node, scope: Scope, mutable: boolean): SymbolInfo | undefined;
  declare(this: Binder, name: string, kind: SymbolKind, node: Node, scope: Scope, mutable: boolean): SymbolInfo;
  predeclareStatements(this: Binder, statements: readonly Statement[], scope: Scope): void;
  predeclareVariableList(this: Binder, list: VariableDeclarationList, scope: Scope): void;
  predeclareImport(this: Binder, node: ImportDeclaration, scope: Scope): void;
  collectFunctionScoped(this: Binder, statements: readonly Statement[], fn: MutableFunction): void;
}

export const declarationMethods: DeclarationMethods = {
  /** Declare every identifier introduced by a (possibly destructured) binding. */
  declareBinding(
    name: BindingName,
    kind: SymbolKind,
    declaration: Node,
    scope: Scope,
    mutable: boolean,
  ): SymbolInfo | undefined {
    if (name.kind === SyntaxKind.Identifier) {
      return this.declare(name.text, kind, declaration, scope, mutable);
    }
    let first: SymbolInfo | undefined;
    const pattern = name as ArrayBindingPattern | ObjectBindingPattern;
    for (const element of pattern.elements) {
      if (!element) continue;
      const symbol = this.declareBinding(element.name, kind, element.name, scope, mutable);
      if (!first) first = symbol;
    }
    return first;
  },

  declare(
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
  },

  /** Pre-declare block-scoped bindings that appear directly in `statements`. */
  predeclareStatements(statements: readonly Statement[], scope: Scope): void {
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
  },

  predeclareVariableList(list: VariableDeclarationList, scope: Scope): void {
    const kind =
      list.declarationKind === "const"
        ? SymbolKind.Const
        : list.declarationKind === "var"
          ? SymbolKind.Var
          : SymbolKind.Let;
    for (const declaration of list.declarations) {
      this.declareBinding(declaration.name, kind, declaration, scope, kind !== SymbolKind.Const);
    }
  },

  predeclareImport(node: ImportDeclaration, scope: Scope): void {
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
  },

  /**
   * Collect `var` and function declarations from nested blocks into the
   * enclosing function scope, without crossing function boundaries.
   */
  collectFunctionScoped(statements: readonly Statement[], fn: MutableFunction): void {
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
              const existing = d.name.kind === SyntaxKind.Identifier ? fn.scope.symbols.get(d.name.text) : undefined;
              if (!existing) this.declareBinding(d.name, SymbolKind.Var, d, fn.scope, true);
              else if (d.name.kind === SyntaxKind.Identifier) this.symbolOfDeclaration.set(d, existing);
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
  },
};
