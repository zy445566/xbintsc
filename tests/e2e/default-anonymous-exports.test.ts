/**
 * `export default function () {}` / `export default class {}` are valid
 * TypeScript. They used to crash the binder because the declaration has no
 * name; the parser now gives them the internal binding name `default` so the
 * bundler can map the default export like any other.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("anonymous default exports", (harness) => {
  it("imports an anonymous default function and class", () => {
    harness.expectSameOutputAsNode(
      `
        import fn from "./lib-fn.ts";
        import C from "./lib-class.ts";
        console.log(fn(), new C().x, new C() instanceof C);
      `,
      {
        files: {
          "lib-fn.ts": "export default function () { return 7; }",
          "lib-class.ts": "export default class { x = 1; }",
        },
      },
    );
  });
});
