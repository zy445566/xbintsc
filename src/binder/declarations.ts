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

/** Kinds that only introduce a type name, never a runtime binding. */
const TYPE_ONLY_KINDS: ReadonlySet<SymbolKind> = new Set([
  SymbolKind.Interface,
  SymbolKind.TypeAlias,
]);

function isVarLike(kind: SymbolKind): boolean {
  return kind === SymbolKind.Var || kind === SymbolKind.Function;
}

/**
 * Whether a declaration of `next` may legally share a scope with `existing`.
 *
 * This is JavaScript's redeclaration rules plus TypeScript's declaration
 * merging, kept deliberately explicit: a wrong `true` would hide a real bug, a
 * wrong `false` would make valid code fail to compile. Only the cases this
 * compiler can actually see are covered; anything unknown is treated as legal.
 */
export function declarationsCanMerge(existing: SymbolKind, next: SymbolKind): boolean {
  if (existing === next) {
    /* Repeated `var` / `function` / `interface` / `enum` declarations merge.
     * Two imports may also share a name: bundling merges every module into one
     * scope, and separate modules routinely import the same builtin
     * (`readFileSync` from `node:fs`, `dirname` from `node:path`). The import
     * map is keyed by symbol id, so keeping the first declaration is enough.
     * `let`, `const`, `class` and parameters do not merge. */
    return (
      existing === SymbolKind.Var ||
      existing === SymbolKind.Function ||
      existing === SymbolKind.Interface ||
      existing === SymbolKind.Namespace ||
      existing === SymbolKind.Enum ||
      existing === SymbolKind.Import
    );
  }
  // `var` and `function` are both function-scoped and may coexist in any order.
  if (isVarLike(existing) && isVarLike(next)) return true;
  // `var` may redeclare a parameter; `let`/`const` may not.
  if (
    (existing === SymbolKind.Parameter && next === SymbolKind.Var) ||
    (existing === SymbolKind.Var && next === SymbolKind.Parameter)
  ) {
    return true;
  }
  // Namespaces merge with classes, functions, enums and each other.
  if (existing === SymbolKind.Namespace || next === SymbolKind.Namespace) return true;
  // A class and an interface merge their instance shape.
  if (
    (existing === SymbolKind.Class && next === SymbolKind.Interface) ||
    (existing === SymbolKind.Interface && next === SymbolKind.Class)
  ) {
    return true;
  }
  // An import binds both a value and a type name, so it conflicts with any
  // same-name local declaration.
  if (existing === SymbolKind.Import || next === SymbolKind.Import) return false;
  // Value and type names live in separate declaration spaces, except classes
  // and enums, which declare a type as well as a value.
  if (TYPE_ONLY_KINDS.has(existing) !== TYPE_ONLY_KINDS.has(next)) {
    const valueKind = TYPE_ONLY_KINDS.has(existing) ? next : existing;
    return valueKind !== SymbolKind.Class && valueKind !== SymbolKind.Enum;
  }
  return false;
}

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
      // `(anonymous)` is the binder's placeholder for `export default
      // function () {}`; those have no user-visible name to collide.
      if (name !== "(anonymous)" && !declarationsCanMerge(existing.kind, kind)) {
        this.conflicts.push({ name, existing, kind, node });
      }
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
        case SyntaxKind.ClassDeclaration: {
          /* `export default class {}` has no binding name; the bundler maps the
           * default export, so there is nothing to pre-declare. */
          const className = (statement as { name?: Identifier }).name?.text;
          if (className !== undefined) {
            this.declare(className, SymbolKind.Class, statement, scope, false);
          }
          break;
        }
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
        const name = (statement as { name?: Identifier }).name?.text;
        if (name === undefined) {
          /* `export default function () {}` / `export default class {}` have no
           * binding name; the default export is handled by the bundler. */
          continue;
        }
        if (!fn.scope.symbols.has(name)) {
          this.declare(name, statement.kind === SyntaxKind.FunctionDeclaration ? SymbolKind.Function : SymbolKind.Class, statement, fn.scope, statement.kind === SyntaxKind.FunctionDeclaration);
        }
        continue;
      }
      visit(statement);
    }
  },
};
