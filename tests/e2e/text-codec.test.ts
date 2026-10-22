/**
 * TextEncoder / TextDecoder: UTF-8 encode to a Uint8Array and decode back.
 * The runtime's strings are already UTF-8, so this is a byte copy in both
 * directions.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("TextEncoder and TextDecoder", (harness) => {
  it("round-trips UTF-8 text through a Uint8Array", () => {
    harness.expectSameOutputAsNode(`
      const encoder = new TextEncoder();
      const bytes = encoder.encode("hé😀");
      console.log(bytes.length, Array.from(bytes).join(","));

      const decoder = new TextDecoder("utf8");
      console.log(decoder.decode(bytes));
      console.log(encoder.encoding, decoder.encoding);
      console.log(encoder.encode().length);
      console.log(decoder.decode());
      console.log(decoder.decode(Uint8Array.from([65, 66, 67])));
    `);
  });
});
