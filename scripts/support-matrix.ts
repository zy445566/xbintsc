#!/usr/bin/env node
/**
 * Render `doc/support-matrix.md` and its Chinese mirror from the vendored
 * test262 catalogue.
 *
 * The catalogue is executable evidence: `tests/conformance/test262.test.ts`
 * compiles and runs every supported case (unmodified test262 files) and
 * requires every unsupported case to fail with the listed diagnostic code.
 * This script turns the same catalogue into the human-readable table, so the
 * document cannot claim a feature the compiler does not exercise.
 *
 * Usage: npm run docs:matrix
 */

import { writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import {
  SUPPORT_AREAS,
  SUPPORTED_CASES,
  UNSUPPORTED_CASES,
  type SupportArea,
} from "../tests/conformance/test262/manifest.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

type Language = "en" | "zh";

export const MATRIX_FILES: Readonly<Record<Language, string>> = {
  en: "doc/support-matrix.md",
  zh: "doc/zh-CN/support-matrix.md",
};

export const TEST262_COMMIT = "d53ebc8b1931d9f1541e43d59650776e4b766248";

function docLink(area: SupportArea, language: Language): string {
  const relative = area.doc.startsWith("doc/") ? area.doc.slice("doc/".length) : area.doc;
  return language === "en" ? relative : `../${relative}`;
}

function areaTitle(area: SupportArea, language: Language): string {
  return language === "en" ? area.title : area.titleZh;
}

function count(area: SupportArea, cases: readonly { readonly area: string }[]): number {
  return cases.filter((testCase) => testCase.area === area.id).length;
}

function rowsFor(language: Language): string {
  return SUPPORT_AREAS.map((area) => {
    const supported = count(area, SUPPORTED_CASES);
    const unsupported = count(area, UNSUPPORTED_CASES);
    return `| ${areaTitle(area, language)} | ${supported} | ${unsupported} | [${language === "en" ? "reference" : "参考"}](${docLink(area, language)}) |`;
  }).join("\n");
}

function supportedTable(language: Language): string {
  const header =
    language === "en"
      ? "| test262 case | Area | What it pins down |\n| --- | --- | --- |"
      : "| test262 用例 | 领域 | 固化的行为 |\n| --- | --- | --- |";
  const rows = SUPPORTED_CASES.map((testCase) => {
    const area = SUPPORT_AREAS.find((candidate) => candidate.id === testCase.area)!;
    const claim = language === "en" ? testCase.claim : testCase.claimZh;
    return `| \`${testCase.path}\` | ${areaTitle(area, language)} | ${claim} |`;
  });
  return [header, ...rows].join("\n");
}

function unsupportedTable(language: Language): string {
  const header =
    language === "en"
      ? "| test262 case | Area | Diagnostic | Why |\n| --- | --- | ---: | --- |"
      : "| test262 用例 | 领域 | 诊断码 | 原因 |\n| --- | --- | ---: | --- |";
  const rows = UNSUPPORTED_CASES.map((testCase) => {
    const area = SUPPORT_AREAS.find((candidate) => candidate.id === testCase.area)!;
    const claim = language === "en" ? testCase.claim : testCase.claimZh;
    return `| \`${testCase.path}\` | ${areaTitle(area, language)} | TS${testCase.code} | ${claim} |`;
  });
  return [header, ...rows].join("\n");
}

export function renderSupportMatrix(language: Language): string {
  const corpusLink =
    language === "en" ? "../tests/conformance/test262/manifest.ts" : "../../tests/conformance/test262/manifest.ts";
  const testLink =
    language === "en" ? "../tests/conformance/test262.test.ts" : "../../tests/conformance/test262.test.ts";
  const sourceLink =
    language === "en" ? "../tests/conformance/test262/SOURCE.md" : "../../tests/conformance/test262/SOURCE.md";

  if (language === "en") {
    return `# Support matrix

> Language: **English** | [简体中文](./zh-CN/support-matrix.md)

> **Generated** by \`npm run docs:matrix\` from the executable catalogue in
> [\`tests/conformance/test262/manifest.ts\`](${corpusLink}). Do not edit this
> file by hand: change a case and regenerate.

Every supported case is an unmodified [test262](https://github.com/tc39/test262)
file (pinned at \`${TEST262_COMMIT}\`) that xbintsc compiles **and runs**; the
[test262 suite](${testLink}) fails if one stops passing. The corpus is a
representative subset, not a full test262 run. Every unsupported case
is valid JavaScript that xbintsc currently rejects, pinned with the diagnostic
code it produces. See [\`SOURCE.md\`](${sourceLink}) for the harness adaptation.
Because the suite runs on every CI platform, this table cannot claim more than
the compiler does.

## Coverage by area

| Area | Supported | Unsupported | Reference |
| --- | ---: | ---: | --- |
${rowsFor(language)}

## Supported cases (test262, compiled and executed)

${supportedTable(language)}

## Unsupported cases (valid JavaScript, expected diagnostics)

${unsupportedTable(language)}
`;
  }

  return `# 支持矩阵

> 语言：[English](../support-matrix.md) | **简体中文**

> 本文件由 \`npm run docs:matrix\` 根据可执行语料
> [\`tests/conformance/test262/manifest.ts\`](${corpusLink}) 生成，请勿手工编辑：
> 修改用例后重新生成。

每个“支持”用例都是 [test262](https://github.com/tc39/test262) 的原始文件
（固定版本 \`${TEST262_COMMIT}\`），由 xbintsc 编译**并执行**；
[test262 测试套件](${testLink}) 会在用例回归时失败。该语料是代表性
子集，不是完整的 test262 全量运行。每个“不支持”用例都是
xbintsc 目前会拒绝的合法 JavaScript，并以实际产生的诊断码固定下来。
harness 的适配说明见 [\`SOURCE.md\`](${sourceLink})。该套件在 CI 的每个平台
运行，因此本表不会声称编译器做不到的能力。

## 各领域覆盖

| 领域 | 支持 | 不支持 | 参考 |
| --- | ---: | ---: | --- |
${rowsFor(language)}

## 支持的用例（test262，编译并执行）

${supportedTable(language)}

## 不支持的用例（合法 JavaScript，预期诊断）

${unsupportedTable(language)}
`;
}

function main(): void {
  for (const language of ["en", "zh"] as const) {
    const path = resolve(root, MATRIX_FILES[language]);
    writeFileSync(path, renderSupportMatrix(language));
    process.stdout.write(`xbintsc: wrote ${MATRIX_FILES[language]}\n`);
  }
}

if (process.argv[1] !== undefined && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  main();
}
