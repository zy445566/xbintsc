import { readdirSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

/**
 * The canonical language documentation is split into one page per compiler
 * stage / feature area under `doc/<area>/`, with an index at `doc/<area>.md`.
 * Nested groups (for example the Node extension) add their own index page
 * (`doc/implemented/node.md`) and pages under its directory.
 *
 * The Chinese translation mirrors the same layout under `doc/zh-CN/`. These
 * tests keep the split honest: adding an English page without its Chinese
 * counterpart, without an index link, or without a language switcher fails here
 * instead of silently going stale. They are cheap and do not need clang.
 */

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

/** Areas that follow the index + page-directory convention. */
const AREAS = ["implemented", "unimplemented"] as const;

function pagesUnder(dir: string, prefix = ""): string[] {
  const pages: string[] = [];
  for (const entry of readdirSync(join(dir, prefix), { withFileTypes: true })) {
    const rel = prefix === "" ? entry.name : `${prefix}/${entry.name}`;
    if (entry.isDirectory()) pages.push(...pagesUnder(dir, rel));
    else if (entry.isFile() && entry.name.endsWith(".md")) pages.push(rel);
  }
  return pages.sort();
}

function toPosix(path: string): string {
  return path.replaceAll("\\", "/");
}

function linkBetween(from: string, to: string): string {
  return toPosix(relative(dirname(from), to));
}

function indexLink(area: string, language: "en" | "zh", page: string): string {
  const group = page.includes("/") ? `${page.slice(0, page.lastIndexOf("/"))}.md` : undefined;
  const index = group
    ? language === "en"
      ? join(root, "doc", area, group)
      : join(root, "doc", "zh-CN", area, group)
    : language === "en"
      ? join(root, "doc", `${area}.md`)
      : join(root, "doc", "zh-CN", `${area}.md`);
  const pagePath =
    language === "en" ? join(root, "doc", area, page) : join(root, "doc", "zh-CN", area, page);
  return linkBetween(index, pagePath);
}

describe.each(AREAS)("%s documentation", (area) => {
  const englishBase = join(root, "doc", area);
  const chineseBase = join(root, "doc", "zh-CN", area);
  const englishPages = pagesUnder(englishBase);
  const chinesePages = pagesUnder(chineseBase);

  it("has a Chinese page for every English page (and vice versa)", () => {
    expect(chinesePages).toEqual(englishPages);
  });

  it("links every page from its index", () => {
    for (const page of englishPages) {
      const englishIndex = readFileSync(
        page.includes("/")
          ? join(englishBase, `${page.slice(0, page.lastIndexOf("/"))}.md`)
          : join(root, "doc", `${area}.md`),
        "utf8",
      );
      const chineseIndex = readFileSync(
        page.includes("/")
          ? join(chineseBase, `${page.slice(0, page.lastIndexOf("/"))}.md`)
          : join(root, "doc", "zh-CN", `${area}.md`),
        "utf8",
      );
      expect(englishIndex).toContain(`./${indexLink(area, "en", page)}`);
      expect(chineseIndex).toContain(`./${indexLink(area, "zh", page)}`);
    }
  });

  it("gives every page a title, an index link and a translation link", () => {
    for (const page of englishPages) {
      const englishPath = join(englishBase, page);
      const chinesePath = join(chineseBase, page);
      const english = readFileSync(englishPath, "utf8");
      const chinese = readFileSync(chinesePath, "utf8");

      expect(english.startsWith("# ")).toBe(true);
      expect(chinese.startsWith("# ")).toBe(true);
      expect(english).toContain("[简体中文]");
      expect(chinese).toContain("[English]");
      expect(english).toContain(linkBetween(englishPath, chinesePath));
      expect(chinese).toContain(linkBetween(chinesePath, englishPath));

      const group = page.includes("/") ? `${page.slice(0, page.lastIndexOf("/"))}.md` : undefined;
      const englishIndexPath = group
        ? join(englishBase, group)
        : join(root, "doc", `${area}.md`);
      const chineseIndexPath = group
        ? join(chineseBase, group)
        : join(root, "doc", "zh-CN", `${area}.md`);
      expect(english).toContain(linkBetween(englishPath, englishIndexPath));
      expect(chinese).toContain(linkBetween(chinesePath, chineseIndexPath));
    }
  });
});
