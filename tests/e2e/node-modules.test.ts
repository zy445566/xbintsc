/**
 * End-to-end tests for the Node compatibility modules that need real host I/O:
 * `buffer`, `fs/promises`, `stream`, `net`, `dgram` and `http`.
 *
 * Every program is compiled to a native binary and executed. The event-loop
 * based modules open servers on an ephemeral port (`listen(0)` / `bind(0)`) so
 * the tests stay hermetic. Skipped when no clang-compatible compiler is found.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

describeWithClang("node compatibility modules", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-node-mods-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function run(source: string): { stdout: string; stderr: string; status: number | null } {
    const entry = join(workdir, `program_${Math.random().toString(36).slice(2)}.ts`);
    writeFileSync(entry, source);
    const extensions = createDefaultRegistry().register(nodeExtension);
    // `force` keeps the runtime objects honest: the whole-program cache does
    // not key on the C sources, only the generated object files do.
    const result = build(entry, { emit: "exe", outDir: join(workdir, "out"), cacheDir, extensions, force: true });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], { encoding: "utf8", timeout: 20000 });
    return { stdout: executed.stdout ?? "", stderr: executed.stderr ?? "", status: executed.status };
  }

  it("implements Buffer construction and instance methods", () => {
    const source = `
      const buf = Buffer.from("hello");
      console.log(buf.toString(), buf.length, Buffer.isBuffer(buf));
      console.log(Buffer.alloc(4, 65).toString());
      console.log(Buffer.from("deadbeef", "hex").toString("hex"));
      console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")]).toString());
      const wide = Buffer.alloc(4);
      wide.writeUInt32BE(305419896, 0);
      console.log(wide.readUInt32BE(0), wide.toString("hex"));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe(
      ["hello 5 true", "AAAA", "deadbeef", "foobar", "305419896 12345678"].join("\n"),
    );
  });

  it("wraps fs operations in promises", () => {
    const target = join(workdir, `promise_${Math.random().toString(36).slice(2)}.txt`);
    const source = `
      import { readFile, rm, stat, writeFile } from "fs/promises";
      async function main(): Promise<void> {
        await writeFile(${JSON.stringify(target)}, "hello promises");
        const data = await readFile(${JSON.stringify(target)});
        const info = await stat(${JSON.stringify(target)});
        console.log(data, info.size, info.isFile());
        await rm(${JSON.stringify(target)});
      }
      main();
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("hello promises 14 true");
  });

  it("streams data through Readable, Transform and pipe", () => {
    const source = `
      const readable = new Readable();
      readable.on("data", (chunk: string) => console.log("data", chunk));
      readable.on("end", () => console.log("end"));
      readable.push("hello");
      readable.push(null);

      const transform = new Transform({ transform(chunk: string, enc: string, cb: (err: unknown, out: string) => void) {
        cb(null, chunk.toUpperCase());
      } });
      transform.on("data", (chunk: string) => console.log("upper", chunk));
      transform.write("abc");

      const source = new Readable();
      const sink = new Writable();
      sink.on("finish", () => console.log("piped"));
      source.pipe(sink);
      source.push("payload");
      source.push(null);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["data hello", "end", "upper ABC", "piped"]);
  });

  it("exchanges data over a TCP server and client", () => {
    const source = `
      const server = net.createServer((socket: any) => {
        socket.on("data", (data: string) => {
          socket.write("echo:" + data);
          socket.end();
        });
      });
      server.listen(0, () => {
        const client = net.connect(server.address().port, "127.0.0.1");
        client.on("data", (data: string) => console.log("client", data));
        client.on("end", () => {
          console.log("end");
          server.close();
        });
        client.write("ping");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["client echo:ping", "end"]);
  });

  it("exchanges a datagram over UDP", () => {
    const source = `
      const server = dgram.createSocket("udp4");
      server.on("message", (msg: string, rinfo: any) => {
        server.send("pong:" + msg, rinfo.port, rinfo.address);
      });
      server.bind(0, () => {
        const client = dgram.createSocket("udp4");
        client.on("message", (msg: string) => {
          console.log(msg);
          client.close();
          server.close();
        });
        client.send("ping", server.address().port, "127.0.0.1");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("pong:ping");
  });

  it("serves HTTP requests and reads responses", () => {
    const source = `
      const server = http.createServer((req: any, res: any) => {
        let body = "";
        req.on("data", (chunk: string) => { body += chunk; });
        req.on("end", () => {
          res.writeHead(200, { "Content-Type": "text/plain" });
          res.end("hello " + req.url + " " + body);
        });
      });
      server.listen(0, () => {
        const port = server.address().port;
        http.get("http://127.0.0.1:" + port + "/world", (res: any) => {
          let body = "";
          console.log("status", res.statusCode);
          res.on("data", (chunk: string) => { body += chunk; });
          res.on("end", () => {
            console.log("get", body);
            const req = http.request({ host: "127.0.0.1", port, path: "/submit", method: "POST" }, (res2: any) => {
              let reply = "";
              res2.on("data", (chunk: string) => { reply += chunk; });
              res2.on("end", () => {
                console.log("post", reply);
                server.close();
              });
            });
            req.write("payload");
            req.end();
          });
        });
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "status 200",
      "get hello /world ",
      "post hello /submit payload",
    ]);
  });
});
