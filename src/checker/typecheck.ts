/**
 * The type-aware half of the checker: declaration/assignment/return/argument
 * assignability, property existence on annotated and inferred shapes, and the
 * small amount of flow narrowing that makes those checks useful inside an
 * `if (value !== null)` block.
 *
 * Every decision is conservative. When the model cannot name a type it uses
 * `any`, and `any` is compatible with everything; a diagnostic is only emitted
 * for a definite mismatch.
 */

import {
  SyntaxKind,
  type ArrowFunction,
  type BinaryExpression,
  type Block,
  type CallExpression,
  type ClassDeclaration,
  type ConditionalExpression,
  type Expression,
  type ForOfStatement,
  type ForStatement,
  type FunctionDeclaration,
  type FunctionExpression,
  type Identifier,
  type IfStatement,
  type NewExpression,
  type ObjectLiteralExpression,
  type PropertyAccessExpression,
  type ReturnStatement,
  type SourceFileNode,
  type Statement,
  type TryStatement,

  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";
import type { BindResult } from "../binder/binder.js";
import { DiagnosticCode, type DiagnosticBag } from "../diagnostics/diagnostic.js";
import { isAssignable, isDefinitelyNotCallable } from "./assignability.js";
import { Flow, narrow, withoutNullish } from "./flow.js";
import { ANY, printType, primitive, unionOf, unwrap, widenType, type Type } from "./type-model.js";
import { TypeResolver } from "./type-system.js";
import { typeCheckerMembers, type TypeCheckerMembers } from "./typecheck-types.js";
import { fileNameOf, isDependencyNode } from "./source.js";

export interface CheckContext {
  readonly returnType: Type;
  readonly thisType?: Type;
}

/**
 * Check declarations, assignments, returns, call arguments and property reads
 * against the type model. Runs after the simpler passes; it only adds
 * `TypeMismatch`, `NotCallable` and `PropertyNotFound` diagnostics.
 */
export function checkTypes(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
): void {
  new TypeChecker(sourceFile, binding, diagnostics).run();
}

export class TypeChecker {
  readonly resolver: TypeResolver;

  constructor(
    readonly sourceFile: SourceFileNode,
    readonly binding: BindResult,
    readonly diagnostics: DiagnosticBag,
  ) {
    this.resolver = new TypeResolver(sourceFile, binding);
  }

  run(): void {
    this.checkStatements(this.sourceFile.statements, new Flow(), { returnType: ANY });
  }

  // -- statements ----------------------------------------------------------

  private checkStatements(statements: readonly Statement[], flow: Flow, context: CheckContext): void {
    for (const statement of statements) this.checkStatement(statement, flow, context);
  }

  private checkStatement(statement: Statement, flow: Flow, context: CheckContext): void {
    if (isDependencyNode(this.sourceFile, statement)) return;
    switch (statement.kind) {
      case SyntaxKind.VariableStatement:
        this.checkVariableList((statement as VariableStatement).declarationList, flow, context);
        return;
      case SyntaxKind.ExpressionStatement:
        this.checkExpression((statement as unknown as { expression: Expression }).expression, flow, context);
        return;
      case SyntaxKind.ReturnStatement: {
        const returned = (statement as ReturnStatement).expression;
        if (returned) this.checkAssignable(returned, context.returnType, flow, context);
        return;
      }
      case SyntaxKind.IfStatement: {
        const condition = (statement as IfStatement).condition;
        this.checkExpression(condition, flow, context);
        this.checkStatement((statement as IfStatement).thenStatement, narrow(condition, true, flow, this.resolver), context);
        const otherwise = (statement as IfStatement).elseStatement;
        if (otherwise) this.checkStatement(otherwise, narrow(condition, false, flow, this.resolver), context);
        return;
      }
      case SyntaxKind.Block:
        this.checkStatements((statement as Block).statements, new Flow(flow), context);
        return;
      case SyntaxKind.FunctionDeclaration:
        this.checkFunction(statement as FunctionDeclaration, flow, context.thisType);
        return;
      case SyntaxKind.ClassDeclaration:
        this.checkClass(statement as ClassDeclaration, flow);
        return;
      case SyntaxKind.ForStatement: {
        const loop = statement as ForStatement;
        const loopFlow = new Flow(flow);
        if (loop.initializer) {
          if (loop.initializer.kind === SyntaxKind.VariableDeclarationList) {
            this.checkVariableList(loop.initializer as VariableDeclarationList, loopFlow, context);
          } else {
            this.checkExpression(loop.initializer as Expression, loopFlow, context);
          }
        }
        if (loop.condition) this.checkExpression(loop.condition, loopFlow, context);
        if (loop.incrementor) this.checkExpression(loop.incrementor, loopFlow, context);
        this.checkStatement(loop.statement, loopFlow, context);
        return;
      }
      case SyntaxKind.ForOfStatement:
        this.checkForOf(statement as ForOfStatement, flow, context);
        return;
      case SyntaxKind.ForInStatement: {
        const loop = statement as unknown as { initializer: VariableDeclarationList | Expression; statement: Statement };
        if (loop.initializer.kind === SyntaxKind.VariableDeclarationList) {
          const declaration = loop.initializer.declarations[0];
          const symbol = declaration ? this.binding.symbolOfDeclaration.get(declaration) : undefined;
          if (symbol && declaration?.name.kind === SyntaxKind.Identifier) this.resolver.setSymbolType(symbol.id, primitive("string"));
        }
        this.checkStatement(loop.statement, new Flow(flow), context);
        return;
      }
      case SyntaxKind.WhileStatement:
      case SyntaxKind.DoStatement: {
        const loop = statement as unknown as { condition: Expression; statement: Statement };
        this.checkExpression(loop.condition, new Flow(flow), context);
        this.checkStatement(loop.statement, new Flow(flow), context);
        return;
      }
      case SyntaxKind.TryStatement: {
        const attempt = statement as TryStatement;
        this.checkStatements(attempt.tryBlock.statements, new Flow(flow), context);
        if (attempt.catchClause) {
          const catchSymbol = attempt.catchClause.variable
            ? this.binding.symbolOfDeclaration.get(attempt.catchClause.variable)
            : undefined;
          if (catchSymbol) this.resolver.setSymbolType(catchSymbol.id, ANY);
          this.checkStatements(attempt.catchClause.block.statements, new Flow(flow), context);
        }
        if (attempt.finallyBlock) this.checkStatements(attempt.finallyBlock.statements, new Flow(flow), context);
        return;
      }
      case SyntaxKind.SwitchStatement: {
        const switchStatement = statement as unknown as { expression: Expression; clauses: { expression?: Expression; statements: Statement[] }[] };
        this.checkExpression(switchStatement.expression, flow, context);
        for (const clause of switchStatement.clauses) {
          if (clause.expression) this.checkExpression(clause.expression, flow, context);
          this.checkStatements(clause.statements, new Flow(flow), context);
        }
        return;
      }
      case SyntaxKind.ThrowStatement:
        this.checkExpression((statement as unknown as { expression: Expression }).expression, flow, context);
        return;
      case SyntaxKind.LabeledStatement:
        this.checkStatement((statement as unknown as { statement: Statement }).statement, flow, context);
        return;
      case SyntaxKind.ExportAssignment:
        this.checkExpression((statement as unknown as { expression: Expression }).expression, flow, context);
        return;
      default:
        return;
    }
  }

  private checkVariableList(list: VariableDeclarationList, flow: Flow, context: CheckContext): void {
    for (const declaration of list.declarations) {
      const symbol = this.binding.symbolOfDeclaration.get(declaration);
      const annotation = declaration.type ? this.resolver.resolve(declaration.type) : undefined;
      const isSimple = declaration.name.kind === SyntaxKind.Identifier;
      if (declaration.initializer) {
        if (annotation && isSimple) this.checkAssignable(declaration.initializer, annotation, flow, context);
        else this.checkExpression(declaration.initializer, flow, context);
      }
      if (!symbol || declaration.name.kind !== SyntaxKind.Identifier) continue;
      const inferred = annotation ?? (declaration.initializer ? this.resolver.inferExpression(declaration.initializer, (candidate) => this.lookup(candidate.id, flow)) : ANY);
      // `let x = 1` has type number, not the literal 1; `const` keeps the literal.
      const widened = list.declarationKind === "const" ? inferred : this.widen(inferred);
      this.resolver.setSymbolType(symbol.id, widened);
      // An alias can mutate the original through a path this pass cannot
      // follow, so the original shape is no longer trusted.
      const initializer = declaration.initializer ? unwrap(declaration.initializer) : undefined;
      if (initializer?.kind === SyntaxKind.Identifier) {
        const source = this.binding.symbolOfIdentifier.get(initializer as Identifier);
        if (source && this.lookup(source.id, flow).kind === "object") this.resolver.setSymbolType(source.id, ANY);
      }
    }
  }

  /** `let`/`var` widen literal and array-literal types (`let x = 1` is number). */
  private widen(type: Type): Type {
    return widenType(type);
  }

  private checkForOf(loop: ForOfStatement, flow: Flow, context: CheckContext): void {
    const iterated = this.checkExpression(loop.expression, flow, context);
    let element: Type = ANY;
    if (iterated.kind === "array") element = iterated.element;
    else if (iterated.kind === "tuple") element = unionOf(iterated.elements);
    else if (iterated.kind === "primitive" && iterated.name === "string") element = primitive("string");
    if (loop.initializer.kind === SyntaxKind.VariableDeclarationList) {
      const declaration = loop.initializer.declarations[0];
      const symbol = declaration ? this.binding.symbolOfDeclaration.get(declaration) : undefined;
      if (symbol && declaration?.name.kind === SyntaxKind.Identifier) {
        const annotated = declaration.type ? this.resolver.resolve(declaration.type) : undefined;
        this.resolver.setSymbolType(symbol.id, annotated ?? element);
      }
    }
    this.checkStatement(loop.statement, new Flow(flow), context);
  }

  private checkFunction(node: FunctionDeclaration, flow: Flow, thisType: Type | undefined): void {
    const returnType = node.returnType ? this.resolver.resolve(node.returnType) : ANY;
    const child = new Flow(flow);
    for (const parameter of node.parameters) {
      const symbol = this.binding.symbolOfDeclaration.get(parameter);
      if (symbol) this.resolver.setSymbolType(symbol.id, parameter.type ? this.resolver.resolve(parameter.type) : ANY);
    }
    const context: CheckContext = { returnType, ...(thisType ? { thisType } : {}) };
    if (!node.body) return;
    this.checkStatements(node.body.statements, child, context);
  }

  private checkClass(node: ClassDeclaration, flow: Flow): void {
    const thisType = this.resolver.classInstanceType(node);
    for (const member of node.members) {
      if (member.kind === SyntaxKind.PropertyDeclaration) {
        const property = member as unknown as { type?: import("../ast/nodes.js").TypeNode; initializer?: Expression };
        if (!property.initializer) continue;
        if (property.type) this.checkAssignable(property.initializer, this.resolver.resolve(property.type), new Flow(flow), { returnType: ANY, thisType });
        else this.checkExpression(property.initializer, new Flow(flow), { returnType: ANY, thisType });
        continue;
      }
      if (member.kind === SyntaxKind.MethodDeclaration || member.kind === SyntaxKind.ConstructorDeclaration) {
        const method = member as unknown as { body?: Block; returnType?: import("../ast/nodes.js").TypeNode; parameters: import("../ast/nodes.js").Parameter[]; modifiers: { modifierKind: string }[] };
        const isStatic = method.modifiers.some((modifier) => modifier.modifierKind === "static");
        if (!method.body) continue;
        const child = new Flow(flow);
        for (const parameter of method.parameters) {
          const symbol = this.binding.symbolOfDeclaration.get(parameter);
          if (symbol) this.resolver.setSymbolType(symbol.id, parameter.type ? this.resolver.resolve(parameter.type) : ANY);
        }
        const context: CheckContext = {
          returnType: method.returnType ? this.resolver.resolve(method.returnType) : ANY,
          ...(isStatic ? {} : { thisType }),
        };
        this.checkStatements(method.body.statements, child, context);
      }
    }
  }

  // -- expressions ---------------------------------------------------------

  lookup(symbolId: number, flow: Flow): Type {
    return flow.typeOf(symbolId) ?? this.resolver.typeOfSymbolId(symbolId) ?? ANY;
  }

  private checkAssignable(expression: Expression, target: Type, flow: Flow, context: CheckContext): Type {
    if (target.kind === "any") return this.checkExpression(expression, flow, context);
    const source = this.checkExpression(expression, flow, context);
    if (!isAssignable(source, target) && !isDependencyNode(this.sourceFile, expression)) {
      this.report(
        DiagnosticCode.TypeMismatch,
        `Type '${printType(source)}' is not assignable to type '${printType(target)}'.`,
        expression,
      );
    }
    return source;
  }

  private report(code: DiagnosticCode, message: string, node: Expression): void {
    this.diagnostics.error(code, message, node, fileNameOf(this.sourceFile, node));
  }

  checkExpression(expression: Expression, flow: Flow, context: CheckContext): Type {
    if (isDependencyNode(this.sourceFile, expression)) return ANY;
    switch (expression.kind) {
      case SyntaxKind.NumericLiteral:
      case SyntaxKind.StringLiteral:
      case SyntaxKind.NoSubstitutionTemplateLiteral:
      case SyntaxKind.BigIntLiteral:
      case SyntaxKind.TrueKeyword:
      case SyntaxKind.FalseKeyword:
      case SyntaxKind.NullKeyword:
      case SyntaxKind.UndefinedKeyword:
        return this.resolver.inferExpression(expression, (symbol) => this.lookup(symbol.id, flow));
      case SyntaxKind.Identifier: {
        const symbol = this.binding.symbolOfIdentifier.get(expression as Identifier);
        return symbol ? this.lookup(symbol.id, flow) : ANY;
      }
      case SyntaxKind.ThisKeyword:
        return context.thisType ?? ANY;
      case SyntaxKind.PropertyAccessExpression: {
        const access = expression as PropertyAccessExpression;
        const base = this.checkExpression(access.expression, flow, context);
        if (access.name.kind !== SyntaxKind.Identifier) return ANY;
        const name = (access.name as Identifier).text;
        const found = this.lookupProperty(base, name);
        if (found.missing) {
          this.report(DiagnosticCode.PropertyNotFound, `Property '${name}' does not exist on ${printType(base)}.`, access);
        }
        return found.type;
      }
      case SyntaxKind.ElementAccessExpression: {
        const access = expression as unknown as { expression: Expression; argumentExpression: Expression };
        const base = this.checkExpression(access.expression, flow, context);
        this.checkExpression(access.argumentExpression, flow, context);
        const index = unwrap(access.argumentExpression);
        // A symbol or computed key is not an array index (`arr[Symbol.iterator]`).
        if (index.kind !== SyntaxKind.NumericLiteral) return ANY;
        if (base.kind === "array") return base.element;
        if (base.kind === "tuple") return base.elements[(index as unknown as { value: number }).value] ?? ANY;
        return ANY;
      }
      case SyntaxKind.CallExpression:
        return this.checkCall(expression as CallExpression, flow, context);
      case SyntaxKind.NewExpression: {
        const call = expression as NewExpression;
        for (const argument of call.arguments) this.checkExpression(argument, flow, context);
        return this.resolver.inferExpression(call, (symbol) => this.lookup(symbol.id, flow));
      }
      case SyntaxKind.BinaryExpression:
        return this.checkBinary(expression as BinaryExpression, flow, context);
      case SyntaxKind.ConditionalExpression: {
        const conditional = expression as ConditionalExpression;
        this.checkExpression(conditional.condition, flow, context);
        const whenTrue = this.checkExpression(conditional.whenTrue, narrow(conditional.condition, true, flow, this.resolver), context);
        const whenFalse = this.checkExpression(conditional.whenFalse, narrow(conditional.condition, false, flow, this.resolver), context);
        return unionOf([whenTrue, whenFalse]);
      }
      case SyntaxKind.ObjectLiteralExpression:
        return this.checkObjectLiteral(expression as ObjectLiteralExpression, flow, context);
      case SyntaxKind.ArrayLiteralExpression: {
        const elements = (expression as unknown as { elements: Expression[] }).elements;
        const types = elements.map((element) => {
          if (element.kind !== SyntaxKind.SpreadElement) return this.checkExpression(element, flow, context);
          const spread = this.checkExpression((element as unknown as { expression: Expression }).expression, flow, context);
          return spread.kind === "array" ? spread.element : ANY;
        });
        return { kind: "array", element: unionOf(types) };
      }
      case SyntaxKind.ArrowFunction:
        this.checkFunctionLike(expression as ArrowFunction, flow, context);
        return this.resolver.inferExpression(expression, (symbol) => this.lookup(symbol.id, flow));
      case SyntaxKind.FunctionExpression:
        this.checkFunctionLike(expression as FunctionExpression, flow, context);
        return this.resolver.inferExpression(expression, (symbol) => this.lookup(symbol.id, flow));
      case SyntaxKind.AsExpression:
        this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
        return this.resolver.resolve((expression as unknown as { type: import("../ast/nodes.js").TypeNode }).type);
      case SyntaxKind.SatisfiesExpression:
      case SyntaxKind.ParenthesizedExpression:
        return this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
      case SyntaxKind.NonNullExpression:
        return withoutNullish(this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context));
      case SyntaxKind.TypeOfExpression:
        this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
        return primitive("string");
      case SyntaxKind.VoidExpression: {
        const operand = (expression as unknown as { expression?: Expression }).expression;
        if (operand) this.checkExpression(operand, flow, context);
        return primitive("undefined");
      }
      case SyntaxKind.PrefixUnaryExpression: {
        const unary = expression as unknown as { operator: string; operand: Expression };
        this.checkExpression(unary.operand, flow, context);
        if (unary.operator === "!") return primitive("boolean");
        if (unary.operator === "typeof") return primitive("string");
        return primitive("number");
      }
      case SyntaxKind.PostfixUnaryExpression:
        this.checkExpression((expression as unknown as { operand: Expression }).operand, flow, context);
        return primitive("number");
      case SyntaxKind.AwaitExpression:
        this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
        return ANY;
      case SyntaxKind.TemplateLiteral: {
        for (const span of (expression as unknown as { spans: { expression: Expression }[] }).spans) this.checkExpression(span.expression, flow, context);
        return primitive("string");
      }
      case SyntaxKind.TaggedTemplateExpression: {
        const tagged = expression as unknown as { tag: Expression; template: Expression };
        this.checkExpression(tagged.tag, flow, context);
        this.checkExpression(tagged.template, flow, context);
        return ANY;
      }
      case SyntaxKind.DeleteExpression:
        this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
        return primitive("boolean");
      case SyntaxKind.SpreadElement:
        return this.checkExpression((expression as unknown as { expression: Expression }).expression, flow, context);
      default:
        return ANY;
    }
  }

  private checkObjectLiteral(literal: ObjectLiteralExpression, flow: Flow, context: CheckContext): Type {
    for (const property of literal.properties) {
      if (property.kind === SyntaxKind.PropertyAssignment) {
        this.checkExpression((property as unknown as { initializer: Expression }).initializer, flow, context);
      } else if (property.kind === SyntaxKind.ShorthandPropertyAssignment) {
        this.checkExpression((property as unknown as { name: Identifier }).name, flow, context);
      } else if (property.kind === SyntaxKind.SpreadElement) {
        this.checkExpression((property as unknown as { expression: Expression }).expression, flow, context);
      }
    }
    return this.resolver.inferExpression(literal, (symbol) => this.lookup(symbol.id, flow));
  }

  private checkFunctionLike(node: ArrowFunction | FunctionExpression, flow: Flow, context: CheckContext): void {
    const returnType = node.returnType ? this.resolver.resolve(node.returnType) : ANY;
    const child = new Flow(flow);
    for (const parameter of node.parameters) {
      const symbol = this.binding.symbolOfDeclaration.get(parameter);
      if (symbol) this.resolver.setSymbolType(symbol.id, parameter.type ? this.resolver.resolve(parameter.type) : ANY);
    }
    const inner: CheckContext = { returnType, ...(context.thisType ? { thisType: context.thisType } : {}) };
    if (node.body.kind === SyntaxKind.Block) this.checkStatements((node.body as Block).statements, child, inner);
    else this.checkAssignable(node.body as Expression, returnType, child, inner);
  }

  private checkBinary(binary: BinaryExpression, flow: Flow, context: CheckContext): Type {
    const assignment = (binary.operator as string) === "=";
    if (assignment) {
      const pattern = unwrap(binary.left);
      // A destructuring assignment target is a pattern, not an object value.
      if (pattern.kind === SyntaxKind.ObjectLiteralExpression || pattern.kind === SyntaxKind.ArrayLiteralExpression) {
        return this.checkExpression(binary.right, flow, context);
      }
      const target = this.checkAssignmentTarget(binary.left, flow, context);
      const value = this.checkExpression(binary.right, flow, context);
      if (!isAssignable(value, target) && !isDependencyNode(this.sourceFile, binary.right)) {
        this.report(DiagnosticCode.TypeMismatch, `Type '${printType(value)}' is not assignable to type '${printType(target)}'.`, binary.right);
      }
      return value;
    }
    if (binary.operator === "&&" || binary.operator === "||") {
      const left = this.checkExpression(binary.left, flow, context);
      const rightFlow = narrow(binary.left, binary.operator === "&&", flow, this.resolver);
      const right = this.checkExpression(binary.right, rightFlow, context);
      return unionOf([left, right]);
    }
    if (binary.operator === "??") {
      const left = this.checkExpression(binary.left, flow, context);
      const right = this.checkExpression(binary.right, narrow(binary.left, true, flow, this.resolver), context);
      return unionOf([withoutNullish(left), right]);
    }
    this.checkExpression(binary.left, flow, context);
    this.checkExpression(binary.right, flow, context);
    return this.resolver.inferExpression(binary, (symbol) => this.lookup(symbol.id, flow));
  }

  /**
   * The type a value is assigned into. A missing property is not reported on a
   * write: xbintsc programs legitimately grow an object after its literal
   * (`const o = {}; o.a = 1`), so the shape is opened instead.
   */
  private checkAssignmentTarget(expression: Expression, flow: Flow, context: CheckContext): Type {
    const target = unwrap(expression);
    if (target.kind === SyntaxKind.Identifier) {
      const symbol = this.binding.symbolOfIdentifier.get(target as Identifier);
      return symbol ? this.lookup(symbol.id, flow) : ANY;
    }
    if (target.kind === SyntaxKind.PropertyAccessExpression) {
      const access = target as PropertyAccessExpression;
      const base = this.checkExpression(access.expression, flow, context);
      if (access.name.kind !== SyntaxKind.Identifier) return ANY;
      const found = this.lookupProperty(base, (access.name as Identifier).text);
      if (!found.missing) return found.type;
      const baseIdentifier = unwrap(access.expression);
      if (baseIdentifier.kind === SyntaxKind.Identifier) {
        const symbol = this.binding.symbolOfIdentifier.get(baseIdentifier as Identifier);
        if (symbol) this.resolver.setSymbolType(symbol.id, ANY);
      }
      return ANY;
    }
    if (target.kind === SyntaxKind.ElementAccessExpression) {
      const access = target as unknown as { expression: Expression; argumentExpression: Expression };
      const base = this.checkExpression(access.expression, flow, context);
      this.checkExpression(access.argumentExpression, flow, context);
      return base.kind === "array" ? base.element : ANY;
    }
    return ANY;
  }

  private checkCall(call: CallExpression, flow: Flow, context: CheckContext): Type {
    this.openMutatedArguments(call, flow);
    const callee = this.checkExpression(call.expression, flow, context);
    if (isDefinitelyNotCallable(callee)) {
      this.report(DiagnosticCode.NotCallable, `This expression is not callable; its type is '${printType(callee)}'.`, call.expression);
      for (const argument of call.arguments) this.checkExpression(argument, flow, context);
      return ANY;
    }
    if (callee.kind !== "function") {
      for (const argument of call.arguments) this.checkExpression(argument, flow, context);
      return ANY;
    }
    const substitutions = this.inferTypeArguments(callee, call, flow, context);
    for (let index = 0; index < call.arguments.length; index++) {
      const argument = call.arguments[index]!;
      if (argument.kind === SyntaxKind.SpreadElement) {
        this.checkExpression((argument as unknown as { expression: Expression }).expression, flow, context);
        continue;
      }
      const last = callee.parameters[callee.parameters.length - 1];
      const parameter = index < callee.parameters.length - 1 ? callee.parameters[index] : last;
      const argumentType = this.checkExpression(argument, flow, context);
      if (!parameter) continue;
      const restElement = parameter.rest && parameter.type.kind === "array" ? parameter.type.element : parameter.type;
      const expected = this.substitute(restElement, substitutions);
      if (!isAssignable(argumentType, expected) && !isDependencyNode(this.sourceFile, argument)) {
        this.report(
          DiagnosticCode.TypeMismatch,
          `Argument of type '${printType(argumentType)}' is not assignable to parameter of type '${printType(expected)}'.`,
          argument,
        );
      }
    }
    return this.substitute(callee.returnType, substitutions);
  }

  /** Calls can mutate an object argument; open its shape when they might. */
  private openMutatedArguments(call: CallExpression, flow: Flow): void {
    if (!this.callMayMutate(call)) return;
    for (const argument of call.arguments) {
      const target = unwrap(argument);
      if (target.kind !== SyntaxKind.Identifier) continue;
      const symbol = this.binding.symbolOfIdentifier.get(target as Identifier);
      if (!symbol) continue;
      if (this.lookup(symbol.id, flow).kind === "object") this.resolver.setSymbolType(symbol.id, ANY);
    }
  }

  private callMayMutate(call: CallExpression): boolean {
    const callee = unwrap(call.expression);
    if (callee.kind !== SyntaxKind.PropertyAccessExpression) return true;
    const access = callee as PropertyAccessExpression;
    if (access.expression.kind !== SyntaxKind.Identifier || access.name.kind !== SyntaxKind.Identifier) return true;
    const owner = (access.expression as Identifier).text;
    const method = (access.name as Identifier).text;
    if (owner === "console" || owner === "String" || owner === "Number" || owner === "Boolean") return false;
    if (owner === "JSON" && method === "stringify") return false;
    if (owner === "Array" && method === "isArray") return false;
    return !(
      owner === "Object" &&
      ["keys", "values", "entries", "getOwnPropertyNames", "getOwnPropertyDescriptor", "getOwnPropertyDescriptors", "getPrototypeOf", "isFrozen", "isSealed", "isExtensible", "hasOwn"].includes(method)
    );
  }
}

// eslint-disable-next-line @typescript-eslint/no-empty-object-type
export interface TypeChecker extends TypeCheckerMembers {}
Object.assign(TypeChecker.prototype, typeCheckerMembers);
