/**
 * Runtime-coverage tests for the Buffer implementation.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: buffer",
  (h) => {
    it("exercises the Buffer implementation", () => {
      const source = `
        const b = Buffer.from("hello");
        console.log(b.toString(), b.length, Buffer.isBuffer(b));
        console.log(Buffer.from([1, 2, 3]).toString("hex"));
        console.log(Buffer.from("68656c6c6f", "hex").toString());
        console.log(Buffer.from("aGVsbG8=", "base64").toString());
        console.log(Buffer.from(b).toString());
        console.log(Buffer.alloc(3).toString("hex"), Buffer.alloc(4, 65).toString(), Buffer.alloc(3, "ab").toString());
        console.log(Buffer.alloc(0).length, Buffer.alloc(3, "").toString("hex"), Buffer.alloc(3, "61", "hex").toString("hex"));
        console.log(Buffer.allocUnsafe(2).length, Buffer.allocUnsafeSlow(2).length);
        console.log(Buffer.byteLength("hello"), Buffer.byteLength("68656c6c6f", "hex"), Buffer.byteLength("aGVsbG8=", "base64"), Buffer.byteLength(b));
        console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")]).toString());
        console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")], 4).toString());
        console.log(Buffer.compare(Buffer.from("a"), Buffer.from("b")), Buffer.compare(Buffer.from("b"), Buffer.from("a")), Buffer.compare(Buffer.from("a"), Buffer.from("a")));
        const n = new Buffer("hey");
        console.log(n.toString(), Buffer.of().length, new Buffer(2).length);
        const j = Buffer.from("hi").toJSON();
        console.log(j.type, j.data.join(","));
        console.log(b.slice(1, 3).toString(), b.slice(-3).toString(), b.subarray(0, 2).toString(), "[" + b.slice(3, 1).toString() + "]");
        console.log(Buffer.from("abc").equals(Buffer.from("abc")), Buffer.from("abc").equals(Buffer.from("abd")), true);
        console.log(Buffer.from("abc").compare(Buffer.from("abd")), Buffer.from("abd").compare(Buffer.from("abc")), Buffer.from("abc").compare(Buffer.from("abc")));
        const target = Buffer.alloc(6);
        const copied = Buffer.from("hello").copy(target, 1, 0, 3);
        console.log(copied, target.toString("hex"));
        const w = Buffer.alloc(8);
        console.log(w.write("abc"), w.write("def", 4), w.toString("hex"));
        console.log(w.write("6161", 0, 4, "hex"), w.toString("hex"));
        console.log(Buffer.alloc(5).fill("a").toString(), Buffer.alloc(5, 1).fill(66, 1, 3).toString("hex"), Buffer.alloc(2).fill(0).toString("hex"));
        console.log(Buffer.from("abc").reverse().toString());
        console.log(b.toString("hex"));
        console.log(Buffer.from("hello world").indexOf("world"), Buffer.from("hello world").indexOf("o"), Buffer.from("hello").indexOf(101), Buffer.from("hello").indexOf(Buffer.from("ll")));
        console.log(Buffer.from("hello world").lastIndexOf("o"), Buffer.from("hello").lastIndexOf("l", 2), Buffer.from("abc").includes("bc"), Buffer.from("abc").includes("z"));
        console.log(Buffer.from("hello").indexOf("z"), Buffer.from("hello").indexOf("toolong"), Buffer.from("hello").indexOf(""), Buffer.from("hello").indexOf("l", -10), Buffer.from("hello").indexOf("l", 100));
        console.log(Buffer.from("hello").lastIndexOf("l", 100), Buffer.from("hello").lastIndexOf("l", -1), Buffer.from("hello").lastIndexOf("lo", 3), Buffer.from("hello").lastIndexOf(111));
        console.log(Buffer.from("hello").indexOf("6c6c", 0, "hex"));
        const io = Buffer.alloc(16);
        console.log(io.writeUInt8(200, 0), io.writeInt8(-56, 1), io.readUInt8(0), io.readInt8(1));
        io.writeUInt16LE(0x1234, 2);
        io.writeUInt16BE(0x1234, 4);
        io.writeUInt16LE(0xfffe, 6);
        io.writeUInt16BE(0xfffe, 8);
        console.log(io.readUInt16LE(2), io.readUInt16BE(4), io.readInt16LE(6), io.readInt16BE(8));
        io.writeUInt32LE(0xffffffff, 0);
        console.log(io.readUInt32LE(0), io.readInt32LE(0));
        io.writeUInt32BE(0x12345678, 4);
        console.log(io.readUInt32BE(4), io.readInt32BE(4));
        console.log(Array.from(Buffer.from("abc").keys()).join(","), Array.from(Buffer.from("abc").values()).join(","));
        console.log(b.toString("base64"), b.toString("hex", 1, 3), "[" + b.toString("utf8", 3, 1) + "]");
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "hello 5 true",
        "010203",
        "hello",
        "hello",
        "hello",
        "000000 AAAA aba",
        "0 000000 616161",
        "2 2",
        "5 5 6 5",
        "foobar",
        "foob",
        "-1 1 0",
        "hey 0 2",
        "Buffer 104,105",
        "el llo he []",
        "true false true",
        "-1 1 0",
        "3 0068656c0000",
        "3 3 6162630064656600",
        "2 6161630064656600",
        "aaaaa 0142420101 0000",
        "cba",
        "68656c6c6f",
        "6 4 1 2",
        "7 2 true false",
        "-1 -1 -1 2 -1",
        "3 3 3 4",
        "2",
        "1 2 200 -56",
        "4660 4660 -2 -2",
        "4294967295 -1",
        "305419896 305419896",
        "0,1,2 97,98,99",
        "aGVsbG8= 656c []",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
