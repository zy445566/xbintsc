/**
 * End-to-end tests for the `readline` Node module.
 *
 * The blocking stdin read runs on the async I/O pool, so these tests feed text
 * through the harness's `input` option and let the event loop deliver it.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("node extension — readline", (harness) => {
  it("emits one line event per input line and closes at end of input", () => {
    const stdout = harness.runProgram(
      `
      import { createInterface } from "readline";

      const rl = createInterface({ input: process.stdin, output: process.stdout, terminal: false });
      const lines: string[] = [];
      rl.on("line", (line: string) => {
        lines.push(line);
        if (line === "quit") {
          console.log("lines=" + lines.join("|"));
          rl.close();
        }
      });
      rl.on("close", () => console.log("closed"));
      `,
      { extensions: true, input: "alpha\nbeta\nquit\n" },
    );
    expect(stdout).toBe("lines=alpha|beta|quit\nclosed");
  });

  it("feeds queued questions in order and writes their prompts", () => {
    const stdout = harness.runProgram(
      `
      import { createInterface } from "readline";

      const rl = createInterface({ input: process.stdin, output: process.stdout });
      rl.question("name? ", (name: string) => {
        console.log("name=" + name);
        rl.question("age? ", (age: string) => {
          console.log("age=" + age);
          rl.close();
        });
      });
      rl.on("close", () => console.log("done"));
      `,
      { extensions: true, input: "ada\n36\n" },
    );
    expect(stdout).toBe("name? name=ada\nage? age=36\ndone");
  });

  it("supports the Interface constructor, once listeners and buffered pause", () => {
    const stdout = harness.runProgram(
      `
      import { Interface } from "readline";

      const rl = new Interface({ input: process.stdin, output: process.stdout });
      rl.pause();
      let first = "";
      let once = 0;
      rl.once("line", (line: string) => { once += 1; first = line; });
      rl.on("line", (line: string) => {
        console.log("got=" + first + ":" + line + ":" + once);
        rl.close();
      });
      rl.resume();
      `,
      { extensions: true, input: "one\ntwo\n" },
    );
    expect(stdout).toBe("got=one:one:1");
  });

  it("writes the Node cursor escape sequences", () => {
    const stdout = harness.runProgram(
      `
      import { clearLine, clearScreenDown, cursorTo, moveCursor } from "readline";

      cursorTo(process.stdout, 2, 3);
      clearLine(process.stdout, 0);
      moveCursor(process.stdout, 1, -1);
      clearScreenDown(process.stdout);
      console.log("done");
      `,
      { extensions: true, raw: true },
    );
    expect(stdout).toBe("\x1b[4;3H\x1b[2K\x1b[1A\x1b[1C\x1b[0Jdone");
  });
});
