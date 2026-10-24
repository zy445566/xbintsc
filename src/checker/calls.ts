/**
 * Call arity: `f(1)` where `f` declares two required parameters.
 *
 * Only calls whose callee the checker can resolve without inference are
 * checked: a direct identifier bound to a function declaration, or to a
 * `const` initialised with a function expression / arrow. Methods, imports,
 * first-class re-exports and reassigned bindings are left alone — guessing
 * there is how a checker produces false positives.
 */

import {
  SyntaxKind,
  type BinaryExpression,
  type CallExpression,
  type Expression,
  type Identifier,
  type Parameter,
  type SourceFileNode,
} from "../ast/nodes.js";
import { SymbolKind, type BindResult, type FunctionInfo } from "../binder/binder.js";
import { DiagnosticCode, type DiagnosticBag } from "../diagnostics/diagnostic.js";
import { ASSIGNMENT_OPERATORS } from "../codegen/generator/tables.js";
import { fileNameOf, isDependencyNode } from "./source.js";
import { walk } from "./walk.js";

interface Arity {
  /** Leading parameters that must be supplied. */
  readonly required: number;
  /** Total parameters, or `Infinity` for a rest parameter. */
  readonly max: number;
}

function arityOf(fn: FunctionInfo): Arity | undefined {
  const parameters = (fn.node as unknown as { parameters?: readonly Parameter[] }).parameters;
  if (!parameters) return undefined;
  let required = 0;
  let max = 0;
  for (const parameter of parameters) {
    if (parameter.dotDotDotToken) return { required, max: Number.POSITIVE_INFINITY };
    max++;
    // A `this:` parameter is a type-only declaration: it is erased and never
    // occupies an argument slot.
    if (parameter.name.kind === SyntaxKind.Identifier && (parameter.name as Identifier).text === "this") {
      max--;
      continue;
    }
    if (!parameter.questionToken && !parameter.initializer) required = max;
  }
  return { required, max };
}

/** Symbols written after their declaration (so their signature can change). */
function collectAssignedSymbols(sourceFile: SourceFileNode, binding: BindResult): Set<number> {
  const assigned = new Set<number>();
  walk(sourceFile, (node) => {
    if (node.kind !== SyntaxKind.BinaryExpression) return;
    const binary = node as BinaryExpression;
    if (!ASSIGNMENT_OPERATORS.has(binary.operator)) return;
    if (binary.left.kind !== SyntaxKind.Identifier) return;
    const symbol = binding.symbolOfIdentifier.get(binary.left as Identifier);
    if (symbol) assigned.add(symbol.id);
  });
  return assigned;
}

function resolveCallee(binding: BindResult, assigned: ReadonlySet<number>, callee: Expression): FunctionInfo | undefined {
  if (callee.kind !== SyntaxKind.Identifier) return undefined;
  const symbol = binding.symbolOfIdentifier.get(callee as Identifier);
  if (!symbol || symbol.declarations.length !== 1 || assigned.has(symbol.id)) return undefined;
  if (symbol.kind === SymbolKind.Function) {
    return binding.functionOfNode.get(symbol.declarations[0]!);
  }
  if (symbol.kind === SymbolKind.Const) {
    const declaration = symbol.declarations[0]!;
    if (declaration.kind !== SyntaxKind.VariableDeclaration) return undefined;
    const initializer = (declaration as unknown as { initializer?: Expression }).initializer;
    if (!initializer) return undefined;
    if (initializer.kind !== SyntaxKind.ArrowFunction && initializer.kind !== SyntaxKind.FunctionExpression) {
      return undefined;
    }
    return binding.functionOfNode.get(initializer);
  }
  return undefined;
}

function plural(count: number): string {
  return count === 1 ? "argument" : "arguments";
}

function expected(arity: Arity): string {
  if (arity.max === Number.POSITIVE_INFINITY) return `at least ${arity.required} ${plural(arity.required)}`;
  if (arity.required === arity.max) return `${arity.required} ${plural(arity.required)}`;
  return `${arity.required}-${arity.max} ${plural(arity.max)}`;
}

export function checkCallArity(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
): void {
  const assigned = collectAssignedSymbols(sourceFile, binding);
  /* `function f() { return arguments.length; }` is a declared-zero-parameter
   * function that legitimately accepts any number of arguments; its parameter
   * list is not the whole signature. */
  const argumentsUsers = new Map<number, boolean>();
  const usesArguments = (fn: FunctionInfo): boolean => {
    const cached = argumentsUsers.get(fn.id);
    if (cached !== undefined) return cached;
    let found = false;
    walk(fn.node, (node) => {
      if (node.kind === SyntaxKind.Identifier && (node as Identifier).text === "arguments") found = true;
    });
    argumentsUsers.set(fn.id, found);
    return found;
  };
  walk(sourceFile, (node) => {
    if (node.kind !== SyntaxKind.CallExpression) return;
    if (isDependencyNode(sourceFile, node)) return;
    const call = node as CallExpression;
    if (call.optional) return;
    if (call.arguments.some((argument) => argument.kind === SyntaxKind.SpreadElement)) return;
    const fn = resolveCallee(binding, assigned, call.expression);
    if (!fn || fn.isModule || usesArguments(fn)) return;
    const arity = arityOf(fn);
    if (!arity) return;
    const argc = call.arguments.length;
    if (argc >= arity.required && argc <= arity.max) return;
    diagnostics.error(
      DiagnosticCode.ArgumentCountMismatch,
      `Expected ${expected(arity)}, but got ${argc}.`,
      call,
      fileNameOf(sourceFile, call),
    );
  });
}
