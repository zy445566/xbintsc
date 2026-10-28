/**
 * GUI `.html` asset loader.
 *
 * `import html from "./index.html"` normally yields the file contents as a
 * string. When the file contains scripts, the loader also compiles them: each
 * script body is wrapped in a function `(window, document) => { … }`,
 * registered through the `__registerScript` builtin, and the element is
 * replaced by an inert `<script data-xt-id="<hash>"></script>` marker. The
 * engine runs the matching functions once the document has been parsed (see
 * `runtime/ext_gui/script.cpp`).
 *
 * This is how xbintsc supports `<script>` without a JavaScript engine: the body
 * is ordinary TypeScript compiled by the existing front end, ahead of time.
 *
 * Inline `<script lang="ts">body</script>` bodies are wrapped directly. External
 * `<script src="./app.ts">` files are read, their top-level `import`/`export
 * … from` statements are hoisted to the generated module (with relative
 * specifiers rewritten so they resolve from the HTML file), and the remaining
 * statements are wrapped in the script function. All scripts run in document
 * order after parsing (there is no "eager" execution point), i.e. every script
 * is effectively `defer`red.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import {
  ModifierKind,
  SyntaxKind,
  type ExportDeclaration,
  type ImportDeclaration,
  type Modifier,
  type Statement,
  type StringLiteral,
} from "../../ast/nodes.js";
import { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { SourceFile } from "../../diagnostics/source.js";
import { Parser } from "../../parser/parser.js";
import { artifactPath } from "../../canonical.js";
import { hashString } from "../../driver/cache.js";
import type { AssetLoadResult, AssetSourceRange } from "../registry.js";
import { inlineStylesheets } from "./css.js";

/** Matches an inline `<script …>body</script>` element, attributes included. */
const SCRIPT_RE = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
/** `<script src>` targets that are URLs (http:, data:, //host, …). */
const URL_SPEC = /^[a-z][a-z0-9+.-]*:/i;

/** Read a (double/single/unquoted) attribute value from a tag's attribute text. */
function attribute(attributes: string, name: string): string | undefined {
  const pattern = new RegExp(
    `(?:^|\\s)${name}\\s*=\\s*(?:"([^"]*)"|'([^']*)'|([^\\s"'=<>\`]+))`,
    "i",
  );
  const match = pattern.exec(attributes);
  if (!match) return undefined;
  return match[1] ?? match[2] ?? match[3];
}

/**
 * Whether a script should be compiled. Defaults to TypeScript for a bare
 * `<script>`; `type="module"` and the usual JS/TS MIME types are accepted,
 * while data blocks (`application/json`, `text/template`, …) are left alone.
 */
function isExecutable(type: string | undefined, lang: string | undefined): boolean {
  if (lang && /^(ts|tsx|typescript|js|jsx|javascript)$/i.test(lang)) return true;
  if (type === undefined) return true;
  switch (type.toLowerCase()) {
    case "module":
    case "text/typescript":
    case "application/typescript":
    case "text/javascript":
    case "application/javascript":
    case "text/ecmascript":
    case "application/ecmascript":
      return true;
    default:
      return false;
  }
}

/** Rewrite a relative import specifier so it resolves from the HTML file. */
function rewriteSpecifier(htmlPath: string, scriptPath: string, specifier: string): string {
  if (!specifier.startsWith(".")) return specifier;
  const absolute = resolve(dirname(scriptPath), specifier);
  let rel = artifactPath(relative(dirname(htmlPath), absolute));
  if (!rel.startsWith(".")) rel = `./${rel}`;
  return rel;
}

/** The original text of an import/re-export with its specifier rewritten. */
function rewriteSpecifierText(
  htmlPath: string,
  scriptPath: string,
  source: string,
  statement: Statement,
  specifier: StringLiteral,
): string {
  const rewritten = rewriteSpecifier(htmlPath, scriptPath, specifier.value);
  return source.slice(statement.start, specifier.start) + JSON.stringify(rewritten) + source.slice(specifier.end, statement.end);
}

/** Every local binding an import declaration introduces. */
function importLocalNames(declaration: ImportDeclaration): string[] {
  const names: string[] = [];
  const clause = declaration.importClause;
  if (!clause) return names;
  if (clause.name) names.push(clause.name.text);
  const bindings = clause.namedBindings;
  if (bindings) {
    if (bindings.kind === SyntaxKind.NamedImports) {
      for (const element of bindings.elements) names.push(element.name.text);
    } else {
      names.push(bindings.name.text);
    }
  }
  return names;
}

interface CompiledModule {
  /** Hoisted `import`/`export … from` statements (specifiers rewritten). */
  imports: string[];
  /** Synthetic imports that ask the bundler to defer a local module. */
  lazyImports: string[];
  /** Statements that load deferred modules at script-run time. */
  prelude: string[];
  /** Local names the hoisted (eager) imports introduce. */
  localNames: string[];
  /** The module body, wrapped as a script function by the caller. */
  body: string;
}

/**
 * A relative script import is evaluated lazily (after `loadHTML`), like a
 * browser module script. Bare specifiers stay eager: they usually name a
 * compiler extension or a package whose top level does not touch the page.
 */
function isDeferredSpecifier(specifier: string): boolean {
  return specifier.startsWith("./") || specifier.startsWith("../") || specifier.startsWith("/") ||
    /^[A-Za-z]:[\\/]/.test(specifier);
}

/** `const { a, b: c } = load();` for a deferred import's named bindings. */
function deferredPrelude(alias: string, declaration: ImportDeclaration): string[] {
  const clause = declaration.importClause;
  if (!clause) return [`${alias}();`];
  const lines: string[] = [];
  if (clause.name) lines.push(`const ${clause.name.text} = ${alias}().default;`);
  const bindings = clause.namedBindings;
  if (bindings && bindings.kind === SyntaxKind.NamedImports) {
    const pairs = bindings.elements
      .filter((element) => !element.isTypeOnly)
      .map((element) => {
        const imported = element.propertyName?.text ?? element.name.text;
        return imported === element.name.text ? element.name.text : `${imported}: ${element.name.text}`;
      });
    if (pairs.length > 0) lines.push(`const { ${pairs.join(", ")} } = ${alias}();`);
  } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
    lines.push(`const ${bindings.name.text} = ${alias}();`);
  }
  if (lines.length === 0) lines.push(`${alias}();`);
  return lines;
}

/** Split an external script module into hoisted imports and a body. */
function compileModuleSource(htmlPath: string, scriptPath: string, scriptIndex: number): CompiledModule {
  /* Mask the normalized text: the parser indexes the `SourceFile` text, where
   * `\r\n` already became `\n`, so blanking statement spans in the raw file
   * would remove the wrong characters on a CRLF checkout. */
  const source = new SourceFile(scriptPath, readFileSync(scriptPath, "utf8")).text;
  const bag = new DiagnosticBag();
  const parsed = new Parser(new SourceFile(scriptPath, source), bag).parseSourceFile();
  const chars = source.split("");
  const imports: string[] = [];
  const lazyImports: string[] = [];
  const prelude: string[] = [];
  const localNames: string[] = [];
  let lazyIndex = 0;
  const blank = (start: number, end: number): void => {
    for (let i = start; i < end; i += 1) chars[i] = " ";
  };
  const hasRuntimeBinding = (clause: ImportDeclaration["importClause"]): boolean => {
    if (!clause) return true;
    if (clause.name) return true;
    const bindings = clause.namedBindings;
    if (!bindings) return false;
    if (bindings.kind === SyntaxKind.NamespaceImport) return true;
    return bindings.elements.some((element) => !element.isTypeOnly);
  };
  const marker = (specifier: string): string => {
    const alias = `__xt_deferred_${scriptIndex}_${lazyIndex}`;
    lazyIndex += 1;
    lazyImports.push(
      `import { __xt_deferred_module as ${alias} } from ${JSON.stringify(rewriteSpecifier(htmlPath, scriptPath, specifier))};`,
    );
    return alias;
  };

  for (const statement of parsed.statements) {
    if (statement.kind === SyntaxKind.ImportDeclaration) {
      const declaration = statement as ImportDeclaration;
      const specifier = declaration.moduleSpecifier.value;
      const clause = declaration.importClause;
      if (
        (clause === undefined || !clause.isTypeOnly) &&
        hasRuntimeBinding(clause) &&
        isDeferredSpecifier(specifier)
      ) {
        const alias = marker(specifier);
        prelude.push(...deferredPrelude(alias, declaration));
        blank(statement.start, statement.end);
        continue;
      }
      /* The front-end has no side-effect `import "x"` form; spell it as an
       * empty named import so the dependency is still loaded. */
      const hoisted = clause
        ? rewriteSpecifierText(htmlPath, scriptPath, source, statement, declaration.moduleSpecifier)
        : `import {} from ${JSON.stringify(rewriteSpecifier(htmlPath, scriptPath, specifier))}`;
      imports.push(hoisted);
      localNames.push(...importLocalNames(declaration));
      blank(statement.start, statement.end);
      continue;
    }
    if (statement.kind === SyntaxKind.ExportDeclaration) {
      const declaration = statement as ExportDeclaration;
      if (declaration.moduleSpecifier) {
        if (isDeferredSpecifier(declaration.moduleSpecifier.value)) {
          const alias = marker(declaration.moduleSpecifier.value);
          prelude.push(`${alias}();`);
        } else {
          imports.push(rewriteSpecifierText(htmlPath, scriptPath, source, statement, declaration.moduleSpecifier));
        }
      }
      blank(statement.start, statement.end);
      continue;
    }
    if (statement.kind === SyntaxKind.ExportAssignment) {
      /* `export default …` is meaningless in a script module; drop it. */
      blank(statement.start, statement.end);
      continue;
    }
    /* Strip `export`/`export default` so the statement is valid inside the
     * generated function. */
    const modifiers = (statement as { modifiers?: Modifier[] }).modifiers ?? [];
    for (const modifier of modifiers) {
      if (modifier.modifierKind === ModifierKind.Export || modifier.modifierKind === ModifierKind.Default) {
        blank(modifier.start, modifier.end);
      }
    }
  }

  return { imports, lazyImports, prelude, localNames, body: chars.join("") };
}

/** Resolve a `<script src>` value to a local file path (ignoring `?`/`#`). */
function resolveScriptPath(htmlPath: string, src: string): string {
  const clean = src.split(/[?#]/, 1)[0] ?? src;
  return resolve(dirname(htmlPath), clean);
}

/** Rewrite `<script>` bodies into registered AOT functions + an HTML string. */
export function loadHtmlAsset(path: string, source: string): AssetLoadResult {
  /* The generated module is parsed as a `SourceFile`, which rewrites `\r\n` to
   * `\n`; normalize first so the module text, the offsets recorded for
   * `<script src>` bodies and the exported HTML string all share one layout. */
  const page = new SourceFile(path, source).text;
  /* `file` is set for an external `<script src>`: its body keeps that file as
   * the origin of every node, so a `.js` script stays untyped and diagnostics
   * name the script, not the HTML page. */
  const scripts: Array<{ id: string; body: string; prelude: string[]; file?: string }> = [];
  const imports: string[] = [];
  const lazyImports: string[] = [];
  const importedText = new Set<string>();
  const bound = new Set<string>();

  /* The engine's cascade only reads `<style>` elements, so a linked stylesheet
   * would be dropped at runtime. Inline it (and its `@import`s) now. */
  const styles = inlineStylesheets(path, page);
  const html = styles.html.replace(SCRIPT_RE, (match, attributes: string, inlineBody: string) => {
    if (!isExecutable(attribute(attributes, "type"), attribute(attributes, "lang"))) return match;
    const src = attribute(attributes, "src");

    if (src !== undefined && !URL_SPEC.test(src) && !src.startsWith("//")) {
      const scriptPath = resolveScriptPath(path, src);
      if (!existsSync(scriptPath)) {
        throw new Error(`gui: <script src="${src}"> not found (looked for ${scriptPath})`);
      }
      const compiled = compileModuleSource(path, scriptPath, scripts.length);
      for (const text of compiled.imports) {
        if (!importedText.has(text)) {
          importedText.add(text);
          imports.push(text);
        }
      }
      lazyImports.push(...compiled.lazyImports);
      for (const name of compiled.localNames) {
        if (bound.has(name)) {
          throw new Error(
            `gui: <script src="${src}"> imports '${name}', already imported by another <script src>; alias it in ${scriptPath}`,
          );
        }
        bound.add(name);
      }
      const id = hashString([...compiled.prelude, compiled.body].join("\n"));
      scripts.push({ id, body: compiled.body, prelude: compiled.prelude, file: scriptPath });
      return `<script data-xt-id="${id}"></script>`;
    }

    /* A `src` URL, or a data block: leave the element untouched. */
    if (src !== undefined) return match;
    if (inlineBody.trim() === "") return match;
    const id = hashString(inlineBody);
    scripts.push({ id, body: inlineBody, prelude: [] });
    return `<script data-xt-id="${id}"></script>`;
  });

  const parts: string[] = [...imports, ...lazyImports];
  const sourceRanges: AssetSourceRange[] = [];
  /* Offset of the next part once joined with "\n", so a body's span in the
   * generated module can be reported back as the file it was copied from. */
  let offset = parts.reduce((total, part) => total + part.length + 1, 0);
  for (const { id, body, prelude, file } of scripts) {
    const header = `function __xt_script_${id}(window: any, document: any): void {\n`;
    const preludeText = prelude.length > 0 ? `${prelude.join("\n")}\n` : "";
    const part =
      `${header}${preludeText}${body}\n}\n` + `__registerScript(${JSON.stringify(id)}, __xt_script_${id});`;
    if (file !== undefined) {
      const bodyStart = offset + header.length + preludeText.length;
      sourceRanges.push({ start: bodyStart, end: bodyStart + body.length, file });
    }
    parts.push(part);
    offset += part.length + 1;
  }
  parts.push(`export default ${JSON.stringify(html)};`);
  const result: AssetLoadResult = {
    moduleSource: parts.join("\n"),
    ...(styles.dependencies.length > 0 ? { dependencies: styles.dependencies } : {}),
  };
  return sourceRanges.length > 0 ? { ...result, sourceRanges } : result;
}
