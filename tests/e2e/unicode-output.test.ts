/**
 * Regression coverage for Chinese (and other non-ASCII) text reaching stdout.
 *
 * The driver reads a source file as UTF-8 bytes, the lexer stores a literal's
 * bytes, and the runtime writes those bytes straight to `stdout`/`stderr`. A
 * mis-encoded literal, a UTF-16 code-unit leak or a console writer that
 * re-encodes the bytes changes the output sequence even when adjacent English
 * text still looks fine — the issue reports exactly that:
 *
 *     console.log('你好世界！');
 *     console.log('Hello World!');
 *
 * The program is run with its stdout/stderr captured as pipes, so these cases
 * pin the bytes the terminal receives without depending on a local code page.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

/** The UTF-8 bytes of `text`, as hexadecimal, so a re-encoding cannot pass. */
function utf8Hex(text: string): string {
  return Buffer.from(text, "utf8").toString("hex");
}

const issueProgram = `console.log('你好世界！');
console.log('Hello World!');
`;

describeE2E("non-ASCII console output", (harness) => {
  it("prints the issue's Chinese and English lines as exact UTF-8", () => {
    const result = harness.runProgramFull(issueProgram, { name: "chinese_issue" });
    expect(result.status).toBe(0);
    expect(result.stderr).toBe("");
    expect(result.stdout).toBe("你好世界！\nHello World!\n");
    expect(utf8Hex(result.stdout)).toBe("e4bda0e5a5bde4b896e7958cefbc810a48656c6c6f20576f726c64210a");
  });

  it("prints Chinese literals, escapes, templates and identifiers byte-identically", () => {
    const result = harness.runProgramFull(
      `
      const 名字 = "世界";
      console.log("你好，" + 名字 + "！");
      console.log("\\u4f60\\u597d\\uff0c\\u4e16\\u754c\\uff01");
      console.log(\`你好，\${名字}！\`);
      console.log(["中文", { 键: "值" }]);
      console.log(JSON.stringify({ 你好: "世界" }));
      `,
      { name: "chinese_values" },
    );
    expect(result.status).toBe(0);
    expect(result.stderr).toBe("");
    expect(result.stdout).toBe(
      [
        "你好，世界！",
        "你好，世界！",
        "你好，世界！",
        "[ '中文', { '键': '值' } ]",
        '{"你好":"世界"}',
        "",
      ].join("\n"),
    );
  });

  it("routes Chinese console levels to the matching stream", () => {
    const result = harness.runProgramFull(
      `
      console.log("日志：中文");
      console.info("信息：中文");
      console.warn("警告：中文");
      console.error("错误：中文");
      `,
      { name: "chinese_console_levels" },
    );
    expect(result.status).toBe(0);
    expect(result.stdout).toBe("日志：中文\n信息：中文\n");
    expect(result.stderr).toBe("警告：中文\n错误：中文\n");
  });
});
