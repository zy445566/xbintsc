import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { ModifierKind, SyntaxKind } from "../../src/ast/nodes.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
function firstStatement(source: string): any {
  const { file, diagnostics } = parse(source);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return file.statements[0];
}

describe("parser module declarations", () => {
  it("parses type-only named imports", () => {
    const declaration = firstStatement('import type { A, B } from "./a";');
    expect(declaration.kind).toBe(SyntaxKind.ImportDeclaration);
    expect(declaration.importClause.isTypeOnly).toBe(true);
    expect(declaration.importClause.namedBindings.kind).toBe(SyntaxKind.NamedImports);
    expect(declaration.importClause.namedBindings.elements).toHaveLength(2);
    expect(declaration.moduleSpecifier.value).toBe("./a");
  });

  it("parses type-only default and namespace imports", () => {
    const defaultImport = firstStatement('import type Foo from "./a";');
    expect(defaultImport.importClause.isTypeOnly).toBe(true);
    expect(defaultImport.importClause.name.text).toBe("Foo");

    const namespaceImport = firstStatement('import type * as ns from "./a";');
    expect(namespaceImport.importClause.isTypeOnly).toBe(true);
    expect(namespaceImport.importClause.namedBindings.kind).toBe(SyntaxKind.NamespaceImport);
    expect(namespaceImport.importClause.namedBindings.name.text).toBe("ns");
  });

  it("parses inline type specifiers and aliases in named imports", () => {
    const declaration = firstStatement('import { type A, B as C } from "./m";');
    const elements = declaration.importClause.namedBindings.elements;
    expect(elements[0].isTypeOnly).toBe(true);
    expect(elements[0].name.text).toBe("A");
    expect(elements[1].propertyName.text).toBe("B");
    expect(elements[1].name.text).toBe("C");
  });

  it("parses import attributes written with `with`", () => {
    const declaration = firstStatement('import data from "./d.json" with { type: "json" };');
    expect(declaration.attributes).toEqual([{ name: "type", value: "json" }]);
  });

  it("parses import assertions written with `assert`", () => {
    const declaration = firstStatement('import data from "./d.json" assert { type: "json" };');
    expect(declaration.attributes).toEqual([{ name: "type", value: "json" }]);
  });

  it("parses `export =` assignments", () => {
    const assignment = firstStatement("export = foo;");
    expect(assignment.kind).toBe(SyntaxKind.ExportAssignment);
    expect(assignment.isExportEquals).toBe(true);
    expect(assignment.expression.kind).toBe(SyntaxKind.Identifier);
  });

  it("parses default expression and declaration exports", () => {
    const expression = firstStatement("export default 1 + 2;");
    expect(expression.kind).toBe(SyntaxKind.ExportAssignment);
    expect(expression.isExportEquals).toBe(false);

    const declaration = firstStatement("export default function f() {}");
    expect(declaration.kind).toBe(SyntaxKind.FunctionDeclaration);
    expect(declaration.modifiers.map((m: any) => m.modifierKind)).toContain(ModifierKind.Default);
  });

  it("parses namespace and wildcard re-exports", () => {
    const namespaced = firstStatement('export * as ns from "./m";');
    expect(namespaced.kind).toBe(SyntaxKind.ExportDeclaration);
    expect(namespaced.exportClause.kind).toBe(SyntaxKind.NamespaceImport);
    expect(namespaced.exportClause.name.text).toBe("ns");
    expect(namespaced.moduleSpecifier.value).toBe("./m");

    const wildcard = firstStatement('export * from "./m";');
    expect(wildcard.kind).toBe(SyntaxKind.ExportDeclaration);
    expect(wildcard.exportClause).toBeUndefined();
    expect(wildcard.moduleSpecifier.value).toBe("./m");
  });

  it("parses type-only wildcard re-exports", () => {
    const declaration = firstStatement('export type * from "./m";');
    expect(declaration.isTypeOnly).toBe(true);
    expect(declaration.moduleSpecifier.value).toBe("./m");
  });

  it("parses named re-exports with aliases and inline types", () => {
    const declaration = firstStatement('export { type A, B as C } from "./m";');
    expect(declaration.kind).toBe(SyntaxKind.ExportDeclaration);
    expect(declaration.exportClause.kind).toBe(SyntaxKind.NamedExports);
    const elements = declaration.exportClause.elements;
    expect(elements[0].isTypeOnly).toBe(true);
    expect(elements[1].propertyName.text).toBe("B");
    expect(elements[1].name.text).toBe("C");
    expect(declaration.moduleSpecifier.value).toBe("./m");
  });

  it("parses type-only named re-exports without a source", () => {
    const declaration = firstStatement("export type { A };");
    expect(declaration.isTypeOnly).toBe(true);
    expect(declaration.exportClause.elements[0].name.text).toBe("A");
    expect(declaration.moduleSpecifier).toBeUndefined();
  });
});
