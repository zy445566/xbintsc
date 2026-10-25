/**
 * GUI extension: inline external stylesheets into the HTML asset.
 *
 * The engine's CSS cascade only reads `<style>` elements (`document.cpp` skips
 * `<link rel="stylesheet">` because resolving it would need I/O at runtime).
 * A page that ships its CSS as a separate file therefore loses *all* styling
 * once it is compiled. The bundler, unlike the runtime, can read files, so this
 * loader rewrites every local stylesheet link (and every local `@import` inside
 * it) into an inline `<style>` block. The page keeps its author-time structure
 * and the compiled binary has no runtime file dependency.
 *
 * Remote URLs are left untouched: there is no network-backed style loader, so
 * the engine simply ignores them, exactly as before.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

/** Read a stylesheet as a byte string (one code unit per UTF-8 byte).
 *
 * The bundler hands asset loaders the same shape (`sourceTextBytes`), so CSS
 * must join the HTML *as bytes*. Decoding it to UTF-16 and putting that into
 * the byte-string document would make codegen encode the HTML's own bytes a
 * second time. `toString("latin1")` is exactly the byte-string mapping; the
 * self-hosted runtime's `readFileSync` already returns bytes. */
function readCssText(file: string): string {
  const data: string | Buffer = readFileSync(file);
  return typeof data === "string" ? data : data.toString("latin1");
}

/** A resolved local stylesheet: its inlined text is returned to the caller. */
export interface InlinedStylesheets {
  readonly html: string;
  /** Every stylesheet file that was read (for bundler cache invalidation). */
  readonly dependencies: readonly string[];
}

/** True for a URL the compiler cannot read (http:, data:, //host, …). */
const URL_SPEC = /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i;

/** Read one attribute from an element's attribute text. */
function attribute(attributes: string, name: string): string | undefined {
  const pattern = new RegExp(
    `(?:^|\\s)${name}\\s*=\\s*(?:"([^"]*)"|'([^']*)'|([^\\s"'=<>\`]+))`,
    "i",
  );
  const match = pattern.exec(attributes);
  if (!match) return undefined;
  return match[1] ?? match[2] ?? match[3];
}

/** Strip a `?query` / `#fragment`, then resolve against a base file. */
function localPath(baseFile: string, specifier: string): string | undefined {
  if (specifier === "" || URL_SPEC.test(specifier)) return undefined;
  const clean = specifier.split(/[?#]/, 1)[0] ?? specifier;
  return resolve(dirname(baseFile), clean);
}

/** True when the `rel` attribute lists `stylesheet` among its tokens. */
function isStylesheet(rel: string | undefined): boolean {
  if (rel === undefined) return false;
  return rel.toLowerCase().split(/\s+/).includes("stylesheet");
}

/**
 * Inline every local `@import` in `css`. Imports are resolved relative to
 * `cssFile`, which is the file the rules came from (not the HTML page), so a
 * partial that imports a sibling keeps working. A cycle is cut at the repeated
 * file rather than recursing forever.
 */
function inlineImports(
  css: string,
  cssFile: string,
  dependencies: Set<string>,
  stack: readonly string[],
): string {
  /* `@import url("./x.css") screen;` and `@import "./x.css";` are the two
   * spellings the CSS parser accepts. Match one at a time so a single bad
   * import cannot swallow the rest of the sheet. */
  const pattern =
    /@import\s+(?:url\(\s*(?:"([^"]*)"|'([^']*)'|([^'")\s]+))\s*\)|"([^"]*)"|'([^']*)')\s*[^;]*;/gi;
  return css.replace(pattern, (match, a, b, c, d, e) => {
    const specifier = a ?? b ?? c ?? d ?? e;
    if (specifier === undefined) return "";
    const file = localPath(cssFile, specifier);
    if (file === undefined) return match;
    if (stack.includes(file)) return "";
    if (!existsSync(file)) return match;
    dependencies.add(file);
    const nested = readCssText(file);
    return inlineImports(nested, file, dependencies, [...stack, file]);
  });
}

/**
 * Replace `<link rel="stylesheet">` elements with inline `<style>` blocks,
 * recursively resolving local `@import` rules. Returns the rewritten HTML and
 * the list of stylesheet files it read.
 */
export function inlineStylesheets(htmlFile: string, html: string): InlinedStylesheets {
  const dependencies = new Set<string>();
  const rewritten = html.replace(/<link\b([^>]*)>/gi, (match, attributes: string) => {
    if (!isStylesheet(attribute(attributes, "rel"))) return match;
    const href = attribute(attributes, "href");
    if (href === undefined) return match;
    const file = localPath(htmlFile, href);
    if (file === undefined || !existsSync(file)) return match;
    dependencies.add(file);
    const css = inlineImports(readCssText(file), file, dependencies, [file]);
    return `<style data-xt-href="${href.replace(/"/g, "&quot;")}">\n${css}\n</style>`;
  });
  return { html: rewritten, dependencies: [...dependencies] };
}
