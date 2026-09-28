/**
 * GUI `.html` asset loader.
 *
 * `import html from "./index.html"` normally yields the file contents as a
 * string. When the file contains inline (or, later, external) scripts, the
 * loader also compiles them: each `<script>` body is wrapped in a function
 * `(window, document) => { … }`, registered through the `__registerScript`
 * builtin, and the element is replaced by an inert
 * `<script data-xt-id="<hash>"></script>` marker. The engine runs the matching
 * functions once the document has been parsed (see `runtime/ext_gui/script.cpp`).
 *
 * This is how xbintsc supports `<script>` without a JavaScript engine: the body
 * is ordinary TypeScript compiled by the existing front end, ahead of time.
 */

import { hashString } from "../../driver/cache.js";
import type { AssetLoadResult } from "../registry.js";

/** Matches an inline `<script …>body</script>` element, attributes included. */
const SCRIPT_RE = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;

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
 * Whether an inline script should be compiled. Defaults to TypeScript for a
 * bare `<script>`; `type="module"` and the usual JS/TS MIME types are accepted,
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

/** Rewrite `<script>` bodies into registered AOT functions + an HTML string. */
export function loadHtmlAsset(_path: string, source: string): AssetLoadResult {
  const scripts: Array<{ id: string; body: string }> = [];

  const html = source.replace(SCRIPT_RE, (match, attributes: string, body: string) => {
    /* External scripts (`src`) are resolved as module imports in M9c. */
    if (attribute(attributes, "src") !== undefined) return match;
    if (!isExecutable(attribute(attributes, "type"), attribute(attributes, "lang"))) return match;
    if (body.trim() === "") return match;
    const id = hashString(body);
    scripts.push({ id, body });
    return `<script data-xt-id="${id}"></script>`;
  });

  const parts: string[] = [];
  for (const { id, body } of scripts) {
    parts.push(
      `function __xt_script_${id}(window: any, document: any): void {\n${body}\n}\n` +
        `__registerScript(${JSON.stringify(id)}, __xt_script_${id});`,
    );
  }
  parts.push(`export default ${JSON.stringify(html)};`);
  return { moduleSource: parts.join("\n") };
}
