/**
 * WHATWG Streams: the web-platform globals and Node's `stream/web` subpath.
 *
 * The differential cases print canonical JSON so Node's console formatting can
 * never mask a difference. Everything is ASCII on purpose: the runtime's
 * non-ASCII string literals are a separate, pre-existing codec issue.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "web streams",
  (h) => {
    it("reads chunks, closes and locks a ReadableStream", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const stream = new ReadableStream<string>({
            start(controller) {
              controller.enqueue("a");
              controller.enqueue("b");
              controller.close();
            },
          });
          const reader = stream.getReader();
          console.log(stream.locked);
          console.log(JSON.stringify(await reader.read()));
          console.log(JSON.stringify(await reader.read()));
          console.log(JSON.stringify(await reader.read()));
          try { stream.getReader(); } catch (error) { console.log((error as Error).name); }
          reader.releaseLock();
          console.log(stream.locked);
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("supports async start, async pull and cancel", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const delayed = new ReadableStream<string>({
            start(controller) {
              setTimeout(() => {
                controller.enqueue("later");
                controller.close();
              }, 1);
            },
          });
          const delayedReader = delayed.getReader();
          console.log(JSON.stringify(await delayedReader.read()));
          console.log(JSON.stringify(await delayedReader.read()));

          let next = 0;
          const pulled = new ReadableStream<number>({
            async pull(controller) {
              await new Promise<void>((resolve) => setTimeout(resolve, 1));
              if (next < 2) controller.enqueue(next++);
              else controller.close();
            },
          });
          const pulledReader = pulled.getReader();
          console.log(JSON.stringify(await pulledReader.read()));
          console.log(JSON.stringify(await pulledReader.read()));
          console.log(JSON.stringify(await pulledReader.read()));

          let reason = "";
          const cancelled = new ReadableStream<string>({
            cancel(value) { reason = String(value); },
          });
          const cancelReader = cancelled.getReader();
          await cancelReader.cancel("stop");
          console.log(reason);
          console.log(JSON.stringify(await cancelReader.read()));
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("writes, closes and aborts a WritableStream", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const received: string[] = [];
          const writable = new WritableStream<string>({
            async write(chunk) {
              await new Promise<void>((resolve) => setTimeout(resolve, 1));
              received.push(chunk);
            },
            close() { console.log("sink closed"); },
          });
          const writer = writable.getWriter();
          console.log(writable.locked);
          await writer.write("a");
          await writer.write("b");
          await writer.close();
          console.log(received.join(","));

          let aborted = "";
          const other = new WritableStream<string>({
            write() {},
            abort(reason) { aborted = String(reason); },
          });
          const otherWriter = other.getWriter();
          await otherWriter.abort("bad");
          console.log(aborted);
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("transforms chunks and pipes through a transform", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const transform = new TransformStream<string, string>({
            transform(chunk, controller) {
              controller.enqueue(chunk.toUpperCase());
            },
            flush(controller) {
              controller.enqueue("!");
            },
          });
          const writer = transform.writable.getWriter();
          const reader = transform.readable.getReader();
          const first = reader.read();
          await writer.write("ab");
          console.log(JSON.stringify(await first));
          const second = reader.read();
          await writer.close();
          console.log(JSON.stringify(await second));
          console.log(JSON.stringify(await reader.read()));

          const received: string[] = [];
          const destination = new WritableStream<string>({
            write(chunk) { received.push(chunk); },
          });
          await ReadableStream.from(["x", "y"]).pipeTo(destination);
          console.log(received.join(","));

          const piped = ReadableStream.from(["p"]).pipeThrough(
            new TransformStream<string, string>({
              transform(chunk, controller) { controller.enqueue(chunk + "?"); },
            }),
          );
          const pipedReader = piped.getReader();
          console.log(JSON.stringify(await pipedReader.read()));
          console.log(JSON.stringify(await pipedReader.read()));
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("builds a stream from an async iterable and propagates transform errors", () => {
      h.expectSameOutputAsNode(
        `
        async function* numbers(): AsyncGenerator<number> {
          yield 1;
          yield 2;
        }

        async function main(): Promise<void> {
          const reader = ReadableStream.from(numbers()).getReader();
          console.log(JSON.stringify(await reader.read()));
          console.log(JSON.stringify(await reader.read()));
          console.log(JSON.stringify(await reader.read()));

          const failing = new TransformStream<string, string>({
            transform() { throw new Error("boom"); },
          });
          const writer = failing.writable.getWriter();
          const failingReader = failing.readable.getReader();
          const readPromise = failingReader.read();
          try { await writer.write("x"); } catch (error) { console.log("write", (error as Error).message); }
          try { await readPromise; } catch (error) { console.log("read", (error as Error).message); }
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("tees a stream and iterates it with for await", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const [first, second] = ReadableStream.from(["1", "2"]).tee();
          const firstReader = first.getReader();
          const secondReader = second.getReader();
          console.log(JSON.stringify(await firstReader.read()), JSON.stringify(await secondReader.read()));
          console.log(JSON.stringify(await firstReader.read()), JSON.stringify(await secondReader.read()));
          console.log(JSON.stringify(await firstReader.read()), JSON.stringify(await secondReader.read()));

          const values: string[] = [];
          for await (const value of ReadableStream.from(["m", "n"])) values.push(value);
          console.log(values.join(","));
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("encodes and decodes text through streams", () => {
      h.expectSameOutputAsNode(
        `
        async function main(): Promise<void> {
          const encoder = new TextEncoderStream();
          const writer = encoder.writable.getWriter();
          const reader = encoder.readable.getReader();
          const chunkPromise = reader.read();
          await writer.write("abc");
          await writer.close();
          const chunk = await chunkPromise;
          console.log(chunk.done, Array.from(chunk.value as Uint8Array).join(","));
          console.log(encoder.encoding);

          const decoder = new TextDecoderStream();
          const decoderWriter = decoder.writable.getWriter();
          const decoderReader = decoder.readable.getReader();
          const decodedPromise = decoderReader.read();
          await decoderWriter.write(Uint8Array.from([104, 105]));
          await decoderWriter.close();
          console.log(JSON.stringify(await decodedPromise));
        }
        main();
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("exposes queuing strategies", () => {
      h.expectSameOutputAsNode(
        `
        const count = new CountQueuingStrategy({ highWaterMark: 3 });
        const bytes = new ByteLengthQueuingStrategy({ highWaterMark: 8 });
        console.log(count.highWaterMark, count.size("anything"));
        console.log(bytes.highWaterMark, bytes.size(new Uint8Array(5)));
        const stream = new ReadableStream<string>({
          start(controller) { controller.enqueue("x"); controller.close(); },
        }, count);
        console.log(stream.locked);
      `,
        { extensions: false, timeout: TIMEOUT },
      );
    });

    it("re-exports the same classes from node:stream/web", () => {
      h.expectSameOutputAsNode(
        `
        import {
          ReadableStream,
          WritableStream,
          TransformStream,
          CountQueuingStrategy,
          ByteLengthQueuingStrategy,
          TextEncoderStream,
          TextDecoderStream,
        } from "node:stream/web";

        async function main(): Promise<void> {
          console.log(
            typeof ReadableStream,
            typeof WritableStream,
            typeof TransformStream,
            typeof CountQueuingStrategy,
            typeof ByteLengthQueuingStrategy,
            typeof TextEncoderStream,
            typeof TextDecoderStream,
          );
          const stream = new ReadableStream<string>({
            start(controller) { controller.enqueue("node"); controller.close(); },
          });
          console.log(stream instanceof ReadableStream);
          console.log(JSON.stringify(await stream.getReader().read()));
        }
        main();
      `,
        { extensions: true, timeout: TIMEOUT },
      );
    });

    it("reports unsupported byte and compression streams explicitly", () => {
      const source = `
        import { CompressionStream, DecompressionStream, ReadableStreamBYOBReader } from "node:stream/web";
        function message(fn: () => unknown): string {
          try { fn(); return "no error"; } catch (error) { return (error as Error).message; }
        }
        const stream = new ReadableStream<string>({});
        console.log(message(() => stream.getReader({ mode: "byob" })));
        console.log(message(() => new CompressionStream("gzip")));
        console.log(message(() => new DecompressionStream("gzip")));
        console.log(typeof ReadableStreamBYOBReader);
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "ReadableStreamBYOBReader is not supported",
        "CompressionStream is not supported in compiled binaries",
        "DecompressionStream is not supported in compiled binaries",
        "function",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-web-streams-" },
);
