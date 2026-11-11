/**
 * Dynamic `import(specifier)` support in the bundler.
 *
 * A dynamic import with a string-literal specifier is a static edge: the target
 * module is bundled like any other, and the import expression is lowered to
 * `Promise.resolve(wrapper())`, where `wrapper` is the target module's
 * namespace-object factory (the same `__xt_deferred_<prefix>` wrapper used for
 * deferred GUI scripts). A non-literal specifier cannot be resolved statically
 * and is left in place for the code generator to reject.
 */

import { SyntaxKind, type CallExpression, type Expression, type Identifier, type ImportExpression, type Node, type PropertyAccessExpression, type SourceFileNode, type StringLiteral } from "../../ast/nodes.js";
import { deferredWrapperName } from "./deferred.js";
import type { ModuleRecord } from "./types.js";

export interface DynamicImportSite {
  readonly expression: ImportExpression;
  readonly specifier: string;
}

function isNode(value: unknown): value is Node {
  return !!value && typeof value === "object" && "kind" in (value as object);
}

/** Pre-order walk over every node in `root` (including the root itself). */
function visitNodes(root: Node, visit: (node: Node) => void): void {
  visit(root);
  const record = root as unknown as Record<string, unknown>;
  for (const key of Object.keys(record)) {
    if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value" || key === "raw") continue;
    const value = record[key];
    if (Array.isArray(value)) {
      for (const child of value) if (isNode(child)) visitNodes(child, visit);
    } else if (isNode(value)) {
      visitNodes(value, visit);
    }
  }
}

/** Every dynamic import with a string-literal specifier, in source order. */
export function collectDynamicImportSites(sourceFile: SourceFileNode): DynamicImportSite[] {
  const sites: DynamicImportSite[] = [];
  visitNodes(sourceFile, (node) => {
    if (node.kind !== SyntaxKind.ImportExpression) return;
    const expression = node as ImportExpression;
    if (expression.specifier.kind === SyntaxKind.StringLiteral) {
      sites.push({ expression, specifier: (expression.specifier as StringLiteral).value });
    }
  });
  return sites;
}

function identifier(text: string, anchor: number): Identifier {
  return { kind: SyntaxKind.Identifier, text, start: anchor, end: anchor };
}

function member(object: string, name: string, anchor: number): PropertyAccessExpression {
  return {
    kind: SyntaxKind.PropertyAccessExpression,
    expression: identifier(object, anchor),
    name: identifier(name, anchor),
    optional: false,
    start: anchor,
    end: anchor,
  };
}

function call(expression: Expression, args: Expression[], anchor: number): CallExpression {
  return { kind: SyntaxKind.CallExpression, expression, typeArguments: [], arguments: args, optional: false, start: anchor, end: anchor };
}

/** `Promise.resolve(wrapper())`, the lowering of a resolved dynamic import. */
function dynamicImportToCall(expression: ImportExpression, wrapperName: string, anchor: number): CallExpression {
  const wrapperCall = call(identifier(wrapperName, anchor), [], anchor);
  return call(member("Promise", "resolve", anchor), [wrapperCall], anchor);
}

/** Children-first walk that lets `replace` swap a node for a new expression. */
function replaceInTree(node: Node, replace: (node: Node) => Node | undefined): Node {
  const record = node as unknown as Record<string, unknown>;
  for (const key of Object.keys(record)) {
    if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value" || key === "raw") continue;
    const value = record[key];
    if (Array.isArray(value)) {
      for (let index = 0; index < value.length; index++) {
        if (isNode(value[index])) value[index] = replaceInTree(value[index] as Node, replace);
      }
    } else if (isNode(value)) {
      record[key] = replaceInTree(value, replace);
    }
  }
  return replace(node) ?? node;
}

/**
 * Rewrite every literal dynamic import in `root` to its resolved namespace
 * wrapper call. `resolve` returns the wrapper name for a file dependency and
 * `undefined` when the specifier is external or unresolved (left in place).
 */
export function rewriteDynamicImports(
  root: Node,
  resolve: (specifier: string) => string | undefined,
  anchor: number,
): void {
  replaceInTree(root, (node) => {
    if (node.kind !== SyntaxKind.ImportExpression) return undefined;
    const expression = node as ImportExpression;
    if (expression.specifier.kind !== SyntaxKind.StringLiteral) return undefined;
    const wrapper = resolve((expression.specifier as StringLiteral).value);
    return wrapper === undefined ? undefined : dynamicImportToCall(expression, wrapper, anchor);
  });
}

/**
 * Rewrite every module's dynamic imports in place and return the set of module
 * paths that are reached only through a dynamic import (so the merge can emit a
 * namespace wrapper for them).
 */
export function prepareDynamicImports(
  records: readonly ModuleRecord[],
  dependencyOf: (record: ModuleRecord, specifier: string) => ModuleRecord | undefined,
): Set<string> {
  const targets = new Set<string>();
  for (const record of records) {
    const resolve = (specifier: string): string | undefined => {
      const dependency = dependencyOf(record, specifier);
      if (dependency) targets.add(dependency.path);
      return dependency ? deferredWrapperName(dependency) : undefined;
    };
    for (const statement of record.sourceFile.statements) {
      rewriteDynamicImports(statement, resolve, record.sourceFile.start);
    }
  }
  return targets;
}
