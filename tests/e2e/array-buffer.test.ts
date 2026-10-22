/**
 * ArrayBuffer and DataView: a byte buffer plus big-endian-by-default accessors,
 * with multiple views sharing the same bytes.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("ArrayBuffer and DataView", (harness) => {
  it("reads and writes integers and floats like Node", () => {
    harness.expectSameOutputAsNode(`
      const buffer = new ArrayBuffer(16);
      const view = new DataView(buffer);

      view.setUint8(0, 255);
      view.setUint16(1, 0x1234);
      console.log(view.getUint8(0), view.getUint16(1), view.getUint16(1, true));

      view.setInt32(3, -2);
      console.log(view.getInt32(3));

      view.setFloat32(0, 1.5);
      console.log(view.getFloat32(0));

      view.setFloat64(8, Math.PI);
      console.log(view.getFloat64(8), view.byteLength);

      const shared = new DataView(buffer);
      console.log(shared.getUint8(0), view.buffer === buffer, shared.buffer === buffer);

      const copy = buffer.slice(0, 4);
      const copyView = new DataView(copy);
      copyView.setUint8(0, 1);
      console.log(copy.byteLength, copyView.getUint8(0), view.getUint8(0));

      console.log(ArrayBuffer.isView(view), ArrayBuffer.isView(new Uint8Array(1)), ArrayBuffer.isView({}));
    `);
  });
});
