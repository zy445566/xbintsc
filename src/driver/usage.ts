/**
 * Static usage analysis for optional link features.
 *
 * The bundled AST is the last place where the compiler can see *all* of a
 * program: imported modules and `.html` assets are already merged into one
 * `SourceFile`, and asset loaders have inlined external stylesheets. This module
 * extracts the facts a {@link LinkFeature} needs to decide whether it may be
 * used, without pretending to be a full data-flow analysis:
 *
 *   - every string literal / template chunk (HTML tags, CSS property values),
 *   - identifiers and property names (`Audio`, `decodeAudioData`, …),
 *   - every `getContext(...)` kind that can be resolved statically,
 *   - whether HTML/CSS can be built from data the compiler cannot see.
 *
 * Constants are resolved through a small, name-based table (`const` only,
 * invalidated by shadowing or reassignment). Anything that cannot be resolved is
 * treated as dynamic, which makes features conservative: a false positive costs
 * size, a false negative could disable a capability at run time.
 */

import {
  SyntaxKind,
  type BinaryExpression,
  type CallExpression,
  type Expression,
  type Identifier,
  type NoSubstitutionTemplateLiteral,
  type Parameter,
  type PrefixUnaryExpression,
  type PrivateIdentifier,
  type PropertyAccessExpression,
  type SourceFileNode,
  type StringLiteral,
  type TemplateLiteral,
  type VariableStatement,
  type ElementAccessExpression,
  type AssignmentExpression,
  type PostfixUnaryExpression,
  type NumericLiteral,
  type BigIntLiteral,
  type BooleanLiteral,
  type Node,
} from "../ast/nodes.js";
import { walk } from "../ast/visitor.js";
import type { ProgramUsage } from "../extensions/link-feature.js";

/** Operators that make a `BinaryExpression`-shaped node an assignment. */
const ASSIGNMENT_OPERATORS = new Set<string>([
  "=",
  "+=",
  "-=",
  "*=",
  "/=",
  "%=",
  "**=",
  "<<=",
  ">>=",
  ">>>=",
  "&=",
  "|=",
  "^=",
  "&&=",
  "||=",
  "??=",
]);

/** Property names whose assigned value is parsed as markup or CSS. */
const MARKUP_PROPERTIES = new Set(["innerHTML", "outerHTML", "cssText"]);

/** A call site with a statically known method/function name. */
interface CallSite {
  readonly name: string;
  readonly arguments: readonly Expression[];
}

/** An assignment whose left side is a statically known property name. */
interface PropertyAssignmentSite {
  readonly name: string;
  readonly right: Expression;
}

/** A wrapper expression (`as`, `satisfies`, `!`, parentheses) around a value. */
interface WrappedExpression {
  readonly expression: Expression;
}

/**
 * The property/method name of `a.b` or `a["b"]`, or the identifier of a bare
 * call (`loadHTML(...)`). Returns undefined for a computed access.
 */
function memberName(expression: Expression): string | undefined {
  if (expression.kind === SyntaxKind.PropertyAccessExpression) {
    return (expression as PropertyAccessExpression).name.text;
  }
  if (expression.kind === SyntaxKind.ElementAccessExpression) {
    const argument = (expression as ElementAccessExpression).argumentExpression;
    if (argument.kind === SyntaxKind.StringLiteral) return (argument as StringLiteral).value;
    return undefined;
  }
  if (expression.kind === SyntaxKind.Identifier) return (expression as Identifier).text;
  return undefined;
}

/** The assignment-shaped view of a `BinaryExpression`, when it is one. */
function asAssignment(node: Node): AssignmentExpression | undefined {
  if (node.kind !== SyntaxKind.BinaryExpression) return undefined;
  const operator = (node as BinaryExpression).operator;
  return ASSIGNMENT_OPERATORS.has(String(operator)) ? (node as unknown as AssignmentExpression) : undefined;
}

/** True for literals that stringify to text which can never contain markup. */
function isPrimitiveLiteral(expression: Expression): boolean {
  switch (expression.kind) {
    case SyntaxKind.NumericLiteral:
    case SyntaxKind.BigIntLiteral:
    case SyntaxKind.TrueKeyword:
    case SyntaxKind.FalseKeyword:
    case SyntaxKind.NullKeyword:
    case SyntaxKind.UndefinedKeyword:
      return true;
    default:
      return false;
  }
}

/** Convert a primitive literal to the text it contributes to a template. */
function primitiveText(expression: Expression): string | undefined {
  switch (expression.kind) {
    case SyntaxKind.NumericLiteral:
      return String((expression as NumericLiteral).value);
    case SyntaxKind.BigIntLiteral:
      return (expression as BigIntLiteral).value.toString();
    case SyntaxKind.TrueKeyword:
    case SyntaxKind.FalseKeyword:
      return (expression as BooleanLiteral).value ? "true" : "false";
    case SyntaxKind.NullKeyword:
      return "null";
    case SyntaxKind.UndefinedKeyword:
      return "undefined";
    default:
      return undefined;
  }
}

/** Collect the usage facts of one bundled program. */
export function collectUsage(sourceFile: SourceFileNode): ProgramUsage {
  const strings = new Set<string>();
  const identifiers = new Set<string>();
  const constants = new Map<string, Expression>();
  const ambiguous = new Set<string>();
  const assigned = new Set<string>();
  const calls: CallSite[] = [];
  const assignments: PropertyAssignmentSite[] = [];
  let createsStyleElement = false;
  let unresolvedTextContent = false;

  const forget = (name: string): void => {
    if (constants.delete(name)) ambiguous.add(name);
  };

  walk(sourceFile, (node) => {
    switch (node.kind) {
      case SyntaxKind.StringLiteral:
        strings.add((node as StringLiteral).value);
        break;
      case SyntaxKind.NoSubstitutionTemplateLiteral:
        strings.add((node as NoSubstitutionTemplateLiteral).value);
        break;
      case SyntaxKind.TemplateLiteral: {
        const template = node as TemplateLiteral;
        strings.add(template.head);
        for (const span of template.spans) strings.add(span.literal);
        break;
      }
      case SyntaxKind.Identifier:
        identifiers.add((node as Identifier).text);
        break;
      case SyntaxKind.PrivateIdentifier:
        identifiers.add((node as PrivateIdentifier).text);
        break;
      case SyntaxKind.VariableStatement: {
        const statement = node as VariableStatement;
        if (statement.declarationList.declarationKind !== "const") break;
        for (const declaration of statement.declarationList.declarations) {
          if (declaration.name.kind !== SyntaxKind.Identifier || declaration.initializer === undefined) continue;
          const name = declaration.name.text;
          /* A second declaration of the same name makes the initializer
           * ambiguous: shadowing is not tracked, so refuse to resolve it. */
          if (constants.has(name) || ambiguous.has(name)) {
            forget(name);
            continue;
          }
          constants.set(name, declaration.initializer);
        }
        break;
      }
      case SyntaxKind.Parameter: {
        /* A parameter shadows any const of the same name; resolution only looks
         * at the name, so mark it ambiguous. */
        const parameter = node as Parameter;
        if (parameter.name.kind === SyntaxKind.Identifier) ambiguous.add(parameter.name.text);
        break;
      }
      case SyntaxKind.PrefixUnaryExpression: {
        const unary = node as PrefixUnaryExpression;
        if (unary.operator === "++" || unary.operator === "--") {
          if (unary.operand.kind === SyntaxKind.Identifier) assigned.add(unary.operand.text);
        }
        break;
      }
      case SyntaxKind.PostfixUnaryExpression: {
        const unary = node as PostfixUnaryExpression;
        if (unary.operand.kind === SyntaxKind.Identifier) assigned.add(unary.operand.text);
        break;
      }
      case SyntaxKind.BinaryExpression: {
        const assignment = asAssignment(node);
        if (assignment === undefined) break;
        if (assignment.left.kind === SyntaxKind.Identifier) assigned.add(assignment.left.text);
        const name = memberName(assignment.left);
        if (name !== undefined) assignments.push({ name, right: assignment.right });
        break;
      }
      case SyntaxKind.CallExpression: {
        const call = node as CallExpression;
        const name = memberName(call.expression);
        if (name !== undefined) calls.push({ name, arguments: call.arguments });
        break;
      }
      default:
        break;
    }
  });

  const resolve = (expression: Expression, stack: Set<string> = new Set()): string | undefined => {
    const primitive = primitiveText(expression);
    if (primitive !== undefined) return primitive;
    switch (expression.kind) {
      case SyntaxKind.StringLiteral:
        return (expression as StringLiteral).value;
      case SyntaxKind.NoSubstitutionTemplateLiteral:
        return (expression as NoSubstitutionTemplateLiteral).value;
      case SyntaxKind.TemplateLiteral: {
        const template = expression as TemplateLiteral;
        let text = template.head;
        for (const span of template.spans) {
          const value = resolve(span.expression, stack);
          if (value === undefined) {
            if (!isPrimitiveLiteral(span.expression)) return undefined;
          } else {
            text += value;
          }
          text += span.literal;
        }
        return text;
      }
      case SyntaxKind.Identifier: {
        const name = (expression as Identifier).text;
        if (assigned.has(name) || ambiguous.has(name)) return undefined;
        const initializer = constants.get(name);
        if (initializer === undefined || stack.has(name)) return undefined;
        stack.add(name);
        const value = resolve(initializer, stack);
        stack.delete(name);
        return value;
      }
      case SyntaxKind.ParenthesizedExpression:
      case SyntaxKind.AsExpression:
      case SyntaxKind.SatisfiesExpression:
      case SyntaxKind.NonNullExpression: {
        return resolve((expression as unknown as WrappedExpression).expression, stack);
      }
      case SyntaxKind.BinaryExpression: {
        const binary = expression as BinaryExpression;
        if (ASSIGNMENT_OPERATORS.has(String(binary.operator)) || binary.operator !== "+") return undefined;
        const left = resolve(binary.left, stack);
        const right = resolve(binary.right, stack);
        return left === undefined || right === undefined ? undefined : left + right;
      }
      default:
        return undefined;
    }
  };

  let dynamicMarkup = false;
  const getContextKinds: (string | undefined)[] = [];

  const sink = (expression: Expression | undefined): void => {
    if (expression === undefined) {
      dynamicMarkup = true;
      return;
    }
    const text = resolve(expression);
    if (text === undefined) dynamicMarkup = true;
    else strings.add(text);
  };

  const sinkAll = (expressions: readonly Expression[]): void => {
    for (const expression of expressions) {
      const text = resolve(expression);
      if (text === undefined) dynamicMarkup = true;
      else strings.add(text);
    }
  };

  for (const site of calls) {
    switch (site.name) {
      case "getContext": {
        const kind = site.arguments[0] === undefined ? undefined : resolve(site.arguments[0]);
        getContextKinds.push(kind === undefined ? undefined : kind.toLowerCase());
        break;
      }
      case "createElement": {
        const tag = site.arguments[0] === undefined ? undefined : resolve(site.arguments[0]);
        if (tag === undefined) dynamicMarkup = true;
        else if (tag.toLowerCase() === "style") createsStyleElement = true;
        break;
      }
      case "createElementNS": {
        const tag = site.arguments[1] === undefined ? undefined : resolve(site.arguments[1]);
        if (tag === undefined) dynamicMarkup = true;
        else if (tag.toLowerCase() === "style") createsStyleElement = true;
        break;
      }
      case "loadHTML":
      case "parseFromString":
      case "write":
      case "writeln":
        sink(site.arguments[0]);
        break;
      case "insertAdjacentHTML":
        sink(site.arguments[1]);
        break;
      case "setAttribute": {
        const attribute = site.arguments[0] === undefined ? undefined : resolve(site.arguments[0]);
        if (attribute === undefined) dynamicMarkup = true;
        else if (attribute.toLowerCase() === "style") sink(site.arguments[1]);
        break;
      }
      case "setProperty": {
        /* The property *name* decides whether a CSS feature is used; the value
         * cannot introduce a new property. A computed name is therefore the
         * only reason to be conservative here. */
        if (site.arguments[0] === undefined || resolve(site.arguments[0]) === undefined) dynamicMarkup = true;
        break;
      }
      case "insertRule":
      case "addRule":
      case "replaceSync":
        sinkAll(site.arguments);
        break;
      default:
        break;
    }
  }

  for (const assignment of assignments) {
    if (MARKUP_PROPERTIES.has(assignment.name)) {
      sink(assignment.right);
    } else if (assignment.name === "textContent" || assignment.name === "innerText") {
      if (resolve(assignment.right) === undefined) unresolvedTextContent = true;
    }
  }

  /* A `<style>` element created at run time is usually filled through
   * `textContent`; if that value is not a literal, CSS (and therefore
   * `mix-blend-mode`) may come from data the compiler cannot see. */
  if (createsStyleElement && unresolvedTextContent) dynamicMarkup = true;

  return {
    strings: [...strings],
    identifiers,
    getContext: getContextKinds,
    dynamicMarkup,
  };
}
