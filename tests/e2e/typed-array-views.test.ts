/**
 * Typed arrays sharing an ArrayBuffer: `new Uint8Array(buffer, offset, len)`,
 * `.buffer`, `subarray` views (writes are visible through every view) and
 * `slice` copies.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("typed array buffer views", (harness) => {
  it("shares bytes between subarray and buffer views", () => {
    harness.expectSameOutputAsNode(`
      const bytes = new Uint8Array([1, 2, 3, 4]);
      const view = bytes.subarray(1, 3);
      console.log(view.length, view[0], view.buffer === bytes.buffer, view.byteOffset);

      view[0] = 9;
      console.log(bytes[1]);

      const copy = bytes.slice(1, 3);
      copy[0] = 7;
      console.log(bytes[1], copy[0]);

      const fromBuffer = new Uint8Array(bytes.buffer, 2, 2);
      console.log(fromBuffer.length, fromBuffer[0]);
      fromBuffer[0] = 5;
      console.log(bytes[2]);

      const words = new Uint16Array(new ArrayBuffer(4));
      words[0] = 0x1234;
      console.log(words[0], new Uint8Array(words.buffer).join(","));

      const dataView = new DataView(bytes.buffer);
      console.log(dataView.getUint8(1));

      console.log(Object.keys(new Uint8Array([1, 2])).join(","));
      console.log(JSON.stringify(new Uint8Array([1, 2])));
    `);
  });
});
