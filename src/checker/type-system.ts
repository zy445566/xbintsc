/**
 * A deliberately small structural type model for the checker.
 *
 * The goal is not to reimplement TypeScript's type system. It is to know
 * enough about annotations, interfaces, classes, arrays, functions and simple
 * generics to answer "is this value definitely the wrong shape?" without
 * guessing. Anything the model does not understand becomes `any`, and `any` is
 * compatible with everything — so unsupported syntax can only hide a bug, never
 * reject working code.
 */

import {
  ModifierKind,
  SyntaxKind,
  type ArrayTypeNode,
  type BinaryExpression,
  type CallExpression,
  type ClassDeclaration,
  type Expression,
  type FunctionDeclaration,
  type FunctionTypeNode,
  type Identifier,
  type InterfaceDeclaration,
  type IntersectionTypeNode,
  type LiteralTypeNode,
  type MethodDeclaration,
  type MethodSignature,
  type Node,
  type Parameter,
  type PropertyAccessExpression,
  type ParenthesizedTypeNode,
  type PropertyDeclaration,
  type PropertySignature,
  type SourceFileNode,
  type TupleTypeNode,
  type TypeAliasDeclaration,
  type TypeLiteralNode,
  type TypeNode,
  type TypeOperatorNode,
  type TypeReference,
  type UnionTypeNode,
  type VariableDeclaration,
} from "../ast/nodes.js";
import { ASSIGNMENT_OPERATORS } from "../codegen/generator/tables.js";
import { classMemberName } from "../binder/helpers.js";
import { SymbolKind, type BindResult, type ClassInfo, type SymbolInfo } from "../binder/binder.js";
import { walk } from "./walk.js";
import {
  ANY,
  isParameterProperty,
  isThisParameter,
  primitive,
  printType,
  unionOf,
  unwrap,
  widenType,
  type ParameterInfo,
  type PropertyInfo,
  type Type,
  type TypeParameterInfo,
} from "./type-model.js";
import { inferTypeOfExpression } from "./type-infer.js";

type TypeDeclaration =
  | { readonly kind: "interface"; readonly node: InterfaceDeclaration }
  | { readonly kind: "alias"; readonly node: TypeAliasDeclaration }
  | { readonly kind: "class"; readonly node: ClassDeclaration };

/** Resolve type syntax and symbol annotations into the small model above. */
export class TypeResolver {
  private readonly declarations = new Map<string, TypeDeclaration | undefined>();
  private readonly symbolTypes = new Map<number, Type>();
  private readonly resolvingSymbols = new Set<number>();
  private readonly resolvingAliases = new Set<string>();
  private readonly resolvingInterfaces = new Set<string>();
  private readonly resolvingClasses = new Set<number>();
  private readonly classCache = new Map<string, Type>();
  private readonly dynamicClasses = new Set<number>();
  private readonly symbolsById = new Map<number, SymbolInfo>();

  constructor(
    readonly sourceFile: SourceFileNode,
    readonly binding: BindResult,
  ) {
    this.collectDeclarations();
    this.collectDynamicClasses();
    for (const symbol of binding.symbolOfDeclaration.values()) this.symbolsById.set(symbol.id, symbol);
  }

  typeOfSymbolId(id: number): Type | undefined {
    const symbol = this.symbolsById.get(id);
    return symbol ? this.typeOfSymbol(symbol) : undefined;
  }

  /** The instance type of the class an `instanceof` right-hand side names. */
  classInstanceFromIdentifier(expression: Expression): Type | undefined {
    const info = this.classOfIdentifier(expression);
    return info ? this.classType(info.node as ClassDeclaration) : undefined;
  }

  // -- declarations --------------------------------------------------------

  private collectDeclarations(): void {
    walk(this.sourceFile, (node) => {
      if (node.kind === SyntaxKind.InterfaceDeclaration) {
        this.addDeclaration((node as InterfaceDeclaration).name.text, { kind: "interface", node: node as InterfaceDeclaration });
      } else if (node.kind === SyntaxKind.TypeAliasDeclaration) {
        this.addDeclaration((node as TypeAliasDeclaration).name.text, { kind: "alias", node: node as TypeAliasDeclaration });
      } else if (node.kind === SyntaxKind.ClassDeclaration) {
        const declaration = node as ClassDeclaration;
        if (declaration.name) this.addDeclaration(declaration.name.text, { kind: "class", node: declaration });
      }
    });
  }

  private addDeclaration(name: string, declaration: TypeDeclaration): void {
    // A duplicate means bundling or shadowing made the name ambiguous; treating
    // it as `any` is the only safe answer without a full binder for types.
    this.declarations.set(name, this.declarations.has(name) ? undefined : declaration);
  }

  private collectDynamicClasses(): void {
    walk(this.sourceFile, (node) => {
      if (node.kind === SyntaxKind.BinaryExpression) {
        const binary = node as BinaryExpression;
        const owner = this.classOfPrototypeExpression(binary.left);
        if (owner) this.dynamicClasses.add(owner.id);
        if (binary.left.kind === SyntaxKind.PropertyAccessExpression) {
          const nested = this.classOfPrototypeExpression((binary.left as unknown as { expression: Expression }).expression);
          if (nested) this.dynamicClasses.add(nested.id);
        }
      } else if (node.kind === SyntaxKind.CallExpression) {
        const call = node as CallExpression;
        const first = call.arguments[0];
        const owner = first ? this.classOfPrototypeExpression(first) : undefined;
        if (owner) this.dynamicClasses.add(owner.id);
      }
    });
    // A constructor that assigns `this[key]` or `Object.assign(this, ...)`
    // creates members no static shape can enumerate.
    for (const info of this.binding.classes) {
      if (this.classHasDynamicMembers(info.node)) this.dynamicClasses.add(info.id);
    }
  }

  private classHasDynamicMembers(node: Node): boolean {
    let dynamic = false;
    walk(node, (child) => {
      if (dynamic) return;
      if (child.kind === SyntaxKind.BinaryExpression) {
        const binary = child as BinaryExpression;
        if (!ASSIGNMENT_OPERATORS.has(binary.operator)) return;
        if (binary.left.kind !== SyntaxKind.ElementAccessExpression) return;
        if (unwrap((binary.left as unknown as { expression: Expression }).expression).kind === SyntaxKind.ThisKeyword) dynamic = true;
        return;
      }
      if (child.kind !== SyntaxKind.CallExpression) return;
      const call = child as CallExpression;
      const callee = call.expression;
      if (callee.kind !== SyntaxKind.PropertyAccessExpression) return;
      const access = callee as PropertyAccessExpression;
      if (access.expression.kind !== SyntaxKind.Identifier || access.name.kind !== SyntaxKind.Identifier) return;
      const owner = (access.expression as Identifier).text;
      const method = (access.name as Identifier).text;
      const mutates = owner === "Object" && (method === "assign" || method === "defineProperty" || method === "defineProperties");
      const first = call.arguments[0];
      if (mutates && first && unwrap(first).kind === SyntaxKind.ThisKeyword) dynamic = true;
    });
    return dynamic;
  }

  /** `this.x = ...` members, typed `any` because the assignment is the only source. */
  private collectThisProperties(node: Node, properties: Map<string, PropertyInfo>): void {
    walk(node, (child) => {
      if (child.kind !== SyntaxKind.BinaryExpression) return;
      const binary = child as BinaryExpression;
      if (!ASSIGNMENT_OPERATORS.has(binary.operator)) return;
      if (binary.left.kind !== SyntaxKind.PropertyAccessExpression) return;
      const access = binary.left as PropertyAccessExpression;
      if (unwrap(access.expression).kind !== SyntaxKind.ThisKeyword) return;
      if (access.name.kind !== SyntaxKind.Identifier) return;
      const name = (access.name as Identifier).text;
      if (!properties.has(name)) properties.set(name, { type: ANY, optional: false });
    });
  }

  private classOfPrototypeExpression(expression: Expression): ClassInfo | undefined {
    const unwrapped = unwrap(expression);
    if (unwrapped.kind !== SyntaxKind.PropertyAccessExpression) return undefined;
    const access = unwrapped as { expression: Expression; name: Identifier };
    if (access.name.kind !== SyntaxKind.Identifier || access.name.text !== "prototype") return undefined;
    return this.classOfIdentifier(access.expression);
  }

  private classOfIdentifier(expression: Expression): ClassInfo | undefined {
    const identifier = unwrap(expression);
    if (identifier.kind !== SyntaxKind.Identifier) return undefined;
    const symbol = this.binding.symbolOfIdentifier.get(identifier as Identifier);
    if (!symbol || symbol.kind !== SymbolKind.Class || symbol.declarations.length !== 1) return undefined;
    return this.binding.classOfNode.get(symbol.declarations[0]!);
  }

  // -- type syntax ---------------------------------------------------------

  resolve(node: TypeNode | undefined, typeParams: ReadonlyMap<string, Type> = new Map()): Type {
    if (!node) return ANY;
    switch (node.kind) {
      case SyntaxKind.AnyKeywordType:
      case SyntaxKind.UnknownKeywordType:
        return ANY;
      case SyntaxKind.NumberKeywordType:
        return primitive("number");
      case SyntaxKind.StringKeywordType:
        return primitive("string");
      case SyntaxKind.BooleanKeywordType:
        return primitive("boolean");
      case SyntaxKind.BigIntKeywordType:
        return primitive("bigint");
      case SyntaxKind.SymbolKeywordType:
        return primitive("symbol");
      case SyntaxKind.NullKeywordType:
        return primitive("null");
      case SyntaxKind.UndefinedKeywordType:
        return primitive("undefined");
      case SyntaxKind.VoidKeywordType:
        return primitive("void");
      case SyntaxKind.NeverKeywordType:
        return ANY;
      case SyntaxKind.ObjectKeywordType:
      case SyntaxKind.ThisKeywordType:
        return ANY;
      case SyntaxKind.LiteralType: {
        const literal = (node as LiteralTypeNode).literal;
        if (literal.kind === SyntaxKind.StringLiteral) return { kind: "literal", value: (literal as { value: string }).value, primitive: "string" };
        if (literal.kind === SyntaxKind.NumericLiteral) return { kind: "literal", value: (literal as { value: number }).value, primitive: "number" };
        if (literal.kind === SyntaxKind.TrueKeyword || literal.kind === SyntaxKind.FalseKeyword) {
          return { kind: "literal", value: literal.kind === SyntaxKind.TrueKeyword, primitive: "boolean" };
        }
        return primitive("null");
      }
      case SyntaxKind.ArrayType:
        return { kind: "array", element: this.resolve((node as ArrayTypeNode).elementType, typeParams) };
      case SyntaxKind.TupleType:
        return { kind: "tuple", elements: (node as TupleTypeNode).elements.map((element) => this.resolve(element, typeParams)) };
      case SyntaxKind.UnionType:
        return unionOf((node as UnionTypeNode).types.map((element) => this.resolve(element, typeParams)));
      case SyntaxKind.IntersectionType: {
        const parts = (node as IntersectionTypeNode).types.map((element) => this.resolve(element, typeParams));
        const properties = new Map<string, PropertyInfo>();
        for (const part of parts) {
          if (part.kind !== "object") return ANY;
          for (const [name, property] of part.properties) properties.set(name, property);
        }
        return { kind: "object", properties, closed: true };
      }
      case SyntaxKind.ParenthesizedType:
        return this.resolve((node as ParenthesizedTypeNode).type, typeParams);
      case SyntaxKind.OptionalType:
      case SyntaxKind.RestType:
        return this.resolve((node as unknown as { type: TypeNode }).type, typeParams);
      case SyntaxKind.TypeOperator:
        return this.resolve((node as TypeOperatorNode).type, typeParams);
      case SyntaxKind.FunctionType:
        return this.functionType(node as FunctionTypeNode, typeParams);
      case SyntaxKind.TypeLiteral:
        return this.typeLiteral(node as TypeLiteralNode, typeParams);
      case SyntaxKind.TypeReference:
        return this.typeReference(node as TypeReference, typeParams);
      default:
        return ANY;
    }
  }

  private functionType(node: FunctionTypeNode, outer: ReadonlyMap<string, Type>): Type {
    const typeParams = new Map(outer);
    const declared: TypeParameterInfo[] = [];
    for (const parameter of node.typeParameters) {
      const name = parameter.name.text;
      typeParams.set(name, { kind: "typeParam", name });
      declared.push({ name });
    }
    return {
      kind: "function",
      parameters: node.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => this.parameterInfo(parameter, typeParams)),
      returnType: this.resolve(node.returnType, typeParams),
      typeParameters: declared,
    };
  }

  parameterInfo(parameter: Parameter, typeParams: ReadonlyMap<string, Type>): ParameterInfo {
    return {
      type: this.resolve(parameter.type, typeParams),
      optional: parameter.questionToken || parameter.initializer !== undefined || parameter.dotDotDotToken,
      rest: parameter.dotDotDotToken,
    };
  }

  private typeLiteral(node: TypeLiteralNode, typeParams: ReadonlyMap<string, Type>): Type {
    const properties = new Map<string, PropertyInfo>();
    let index: Type | undefined;
    for (const member of node.members) {
      if (member.kind === SyntaxKind.PropertySignature) {
        const property = member as PropertySignature;
        this.addProperty(properties, classMemberName(property.name), this.resolve(property.type, typeParams), property.questionToken);
      } else if (member.kind === SyntaxKind.MethodSignature) {
        const method = member as MethodSignature;
        properties.set(classMemberName(method.name), {
          type: {
            kind: "function",
            parameters: method.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => this.parameterInfo(parameter, typeParams)),
            returnType: this.resolve(method.returnType, typeParams),
            typeParameters: method.typeParameters.map((parameter) => ({ name: parameter.name.text })),
          },
          optional: method.questionToken,
        });
      } else if (member.kind === SyntaxKind.IndexSignature) {
        const parameter = (member as unknown as { parameters: Parameter[] }).parameters[0];
        index = this.resolve(parameter?.type, typeParams);
      }
    }
    return index ? { kind: "object", properties, index, closed: false } : { kind: "object", properties, closed: true };
  }

  private addProperty(properties: Map<string, PropertyInfo>, name: string, type: Type, optional: boolean): void {
    if (!name) return;
    properties.set(name, { type, optional });
  }

  private typeReference(node: TypeReference, typeParams: ReadonlyMap<string, Type>): Type {
    const name = node.typeName.kind === SyntaxKind.Identifier ? (node.typeName as Identifier).text : "";
    if (!name) return ANY;
    if (typeParams.has(name)) return typeParams.get(name)!;
    const builtin = this.builtinType(name, node.typeArguments.map((argument) => this.resolve(argument, typeParams)));
    if (builtin) return builtin;
    const declaration = this.declarations.get(name);
    if (!declaration) return { kind: "typeParam", name };
    if (declaration.kind === "interface") return this.interfaceType(declaration.node, node.typeArguments.map((argument) => this.resolve(argument, typeParams)));
    if (declaration.kind === "alias") return this.aliasType(declaration.node, node.typeArguments.map((argument) => this.resolve(argument, typeParams)));
    return this.classType(declaration.node, node.typeArguments.map((argument) => this.resolve(argument, typeParams)));
  }

  private builtinType(name: string, args: readonly Type[]): Type | undefined {
    switch (name) {
      case "Array":
      case "ReadonlyArray":
        return { kind: "array", element: args[0] ?? ANY };
      case "String":
        return primitive("string");
      case "Number":
        return primitive("number");
      case "Boolean":
        return primitive("boolean");
      case "Record":
        return { kind: "object", properties: new Map(), index: args[1] ?? ANY, closed: false };
      case "Partial":
      case "Required":
      case "Readonly":
      case "Pick":
      case "Omit":
      case "Exclude":
      case "Extract":
      case "NonNullable":
      case "ReturnType":
      case "Parameters":
      case "Promise":
      case "PromiseLike":
      case "Map":
      case "Set":
      case "WeakMap":
      case "WeakSet":
      case "Date":
      case "RegExp":
      case "Error":
      case "Function":
      case "Object":
        return ANY;
      default:
        return undefined;
    }
  }

  private bindTypeParameters(declaration: { typeParameters: readonly { name: Identifier }[] }, args: readonly Type[]): Map<string, Type> {
    const params = new Map<string, Type>();
    declaration.typeParameters.forEach((parameter, index) => {
      params.set(parameter.name.text, args[index] ?? { kind: "typeParam", name: parameter.name.text });
    });
    return params;
  }

  private aliasType(node: TypeAliasDeclaration, args: readonly Type[]): Type {
    const name = node.name.text;
    if (this.resolvingAliases.has(name)) return ANY;
    this.resolvingAliases.add(name);
    const params = this.bindTypeParameters(node, args);
    const resolved = this.resolve(node.type, params);
    this.resolvingAliases.delete(name);
    return resolved;
  }

  private interfaceType(node: InterfaceDeclaration, args: readonly Type[] = []): Type {
    const name = node.name.text;
    if (this.resolvingInterfaces.has(name)) return ANY;
    this.resolvingInterfaces.add(name);
    const result = this.buildInterfaceType(node, args);
    this.resolvingInterfaces.delete(name);
    return result;
  }

  private buildInterfaceType(node: InterfaceDeclaration, args: readonly Type[] = []): Type {
    const params = this.bindTypeParameters(node, args);
    const properties = new Map<string, PropertyInfo>();
    let index: Type | undefined;
    for (const clause of node.heritage) {
      if (clause.token !== "extends") continue;
      const parent = clause.types[0]?.expression;
      if (!parent || parent.kind !== SyntaxKind.Identifier) return ANY;
      const parentDecl = this.declarations.get((parent as Identifier).text);
      if (!parentDecl || parentDecl.kind !== "interface") return ANY;
      const parentType = this.interfaceType(parentDecl.node, []);
      if (parentType.kind !== "object") return ANY;
      for (const [name, property] of parentType.properties) properties.set(name, property);
    }
    for (const member of node.members) {
      if (member.kind === SyntaxKind.PropertySignature) {
        const property = member as PropertySignature;
        this.addProperty(properties, classMemberName(property.name), this.resolve(property.type, params), property.questionToken);
      } else if (member.kind === SyntaxKind.MethodSignature) {
        const method = member as MethodSignature;
        properties.set(classMemberName(method.name), {
          type: {
            kind: "function",
            parameters: method.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => this.parameterInfo(parameter, params)),
            returnType: this.resolve(method.returnType, params),
            typeParameters: method.typeParameters.map((parameter) => ({ name: parameter.name.text })),
          },
          optional: method.questionToken,
        });
      } else if (member.kind === SyntaxKind.IndexSignature) {
        const parameter = (member as unknown as { parameters: Parameter[] }).parameters[0];
        index = this.resolve(parameter?.type, params);
      }
    }
    return index ? { kind: "object", properties, index, closed: false, label: node.name.text } : { kind: "object", properties, closed: true, label: node.name.text };
  }

  private classType(node: ClassDeclaration, args: readonly Type[] = []): Type {
    const info = this.binding.classOfNode.get(node);
    if (!info) return ANY;
    if (this.resolvingClasses.has(info.id)) return ANY;
    this.resolvingClasses.add(info.id);
    const result = this.buildClassType(info, node, args);
    this.resolvingClasses.delete(info.id);
    return result;
  }

  private buildClassType(info: ClassInfo, node: ClassDeclaration, args: readonly Type[]): Type {
    if (this.dynamicClasses.has(info.id)) return ANY;
    if (info.parentExpression && !info.parent) return ANY;
    const cacheKey = `${info.id}<${args.map((argument) => printType(argument)).join(",")}>`;
    const cached = this.classCache.get(cacheKey);
    if (cached) return cached;
    const params = this.bindTypeParameters(node, args);
    const properties = new Map<string, PropertyInfo>();
    if (info.parent) {
      const parentType = this.classType(info.parent.node as ClassDeclaration, []);
      if (parentType.kind !== "object") return ANY;
      for (const [name, property] of parentType.properties) properties.set(name, property);
    }
    for (const member of node.members) {
      if (member.kind === SyntaxKind.PropertyDeclaration) {
        const property = member as PropertyDeclaration;
        if (property.modifiers.some((modifier) => modifier.modifierKind === ModifierKind.Static)) continue;
        const name = classMemberName(property.name);
        const type = property.type ? this.resolve(property.type, params) : property.initializer ? widenType(this.inferExpression(property.initializer)) : ANY;
        this.addProperty(properties, name, type, property.questionToken);
      } else if (member.kind === SyntaxKind.MethodDeclaration) {
        const method = member as MethodDeclaration;
        if (method.modifiers.some((modifier) => modifier.modifierKind === ModifierKind.Static)) continue;
        const name = classMemberName(method.name);
        if (method.accessor === "get") {
          properties.set(name, { type: this.resolve(method.returnType, params), optional: false });
        } else if (method.accessor === "set") {
          if (!properties.has(name)) {
            const parameter = method.parameters[0];
            properties.set(name, { type: parameter ? this.resolve(parameter.type, params) : ANY, optional: false });
          }
        } else {
          properties.set(name, {
            type: {
              kind: "function",
              parameters: method.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => this.parameterInfo(parameter, params)),
              returnType: this.resolve(method.returnType, params),
              typeParameters: [],
            },
            optional: method.optional,
          });
        }
      }
    }
    this.collectThisProperties(node, properties);
    if (info.ctor) {
      const parameters = (info.ctor.node as unknown as { parameters?: Parameter[] }).parameters ?? [];
      for (const parameter of parameters) {
        if (!isParameterProperty(parameter)) continue;
        if (parameter.name.kind !== SyntaxKind.Identifier) continue;
        properties.set(parameter.name.text, { type: this.resolve(parameter.type, params), optional: parameter.questionToken });
      }
    }
    const type: Type = { kind: "object", properties, closed: true, label: `class '${node.name?.text ?? info.name}'` };
    this.classCache.set(cacheKey, type);
    return type;
  }

  /** The instance shape of a class declaration (`this` inside its methods). */
  classInstanceType(node: ClassDeclaration): Type {
    return this.classType(node);
  }

  // -- symbols -------------------------------------------------------------

  setSymbolType(symbolId: number, type: Type): void {
    this.symbolTypes.set(symbolId, type);
  }

  typeOfSymbol(symbol: SymbolInfo): Type {
    const cached = this.symbolTypes.get(symbol.id);
    if (cached) return cached;
    if (this.resolvingSymbols.has(symbol.id)) return ANY;
    this.resolvingSymbols.add(symbol.id);
    const type = this.computeSymbolType(symbol);
    this.resolvingSymbols.delete(symbol.id);
    this.symbolTypes.set(symbol.id, type);
    return type;
  }

  private computeSymbolType(symbol: SymbolInfo): Type {
    const declaration = symbol.declarations[0];
    if (!declaration) return ANY;
    if (symbol.kind === SymbolKind.Import) return ANY;
    if (symbol.kind === SymbolKind.Class) return ANY;
    if (symbol.kind === SymbolKind.Function && declaration.kind === SyntaxKind.FunctionDeclaration) {
      // Overloads: no single signature is authoritative.
      if (symbol.declarations.length > 1) return ANY;
      const fn = declaration as FunctionDeclaration;
      const typeParams = new Map<string, Type>();
      const declared: TypeParameterInfo[] = [];
      for (const parameter of fn.typeParameters) {
        typeParams.set(parameter.name.text, { kind: "typeParam", name: parameter.name.text });
        declared.push({ name: parameter.name.text });
      }
      return {
        kind: "function",
        parameters: fn.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => this.parameterInfo(parameter, typeParams)),
        returnType: this.resolve(fn.returnType, typeParams),
        typeParameters: declared,
        label: symbol.name,
      };
    }
    if (declaration.kind === SyntaxKind.VariableDeclaration) {
      const variable = declaration as VariableDeclaration;
      if (variable.type) return this.resolve(variable.type);
      if (variable.initializer) return this.inferExpression(variable.initializer);
      return ANY;
    }
    if (declaration.kind === SyntaxKind.Parameter) {
      const parameter = declaration as Parameter;
      return this.resolve(parameter.type);
    }
    return ANY;
  }


  inferExpression(expression: Expression, lookup?: (symbol: SymbolInfo) => Type | undefined): Type {
    return inferTypeOfExpression(this, expression, lookup);
  }
}
