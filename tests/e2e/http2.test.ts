/**
 * End-to-end coverage for HTTP/2: the compiled client against Node's
 * `http2` server (cleartext h2c and TLS/ALPN) and Node's client against a
 * compiled server. Requires the `nghttp2` and `tls` archives built by
 * `npm run tls`; otherwise the suite is skipped.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { findRuntimeLibrary } from "../../src/driver/runtime-lib.js";
import { hasClang } from "../helpers.js";
import { runBinary, runNode, startBinaryServer, startNodeServer } from "./net-helpers.js";

const fixtureDir = join(dirname(fileURLToPath(import.meta.url)), "..", "fixtures", "tls");
const certificatePath = join(fixtureDir, "cert.pem");
const keyPath = join(fixtureDir, "key.pem");
const http2Available =
  findRuntimeLibrary("nghttp2") !== undefined && findRuntimeLibrary("tls") !== undefined;

const NODE_SERVER = `
const http2 = require("node:http2");
const fs = require("node:fs");
const [mode, cert, key, portFile] = process.argv.slice(2);
const server =
  mode === "secure"
    ? http2.createSecureServer({ cert: fs.readFileSync(cert), key: fs.readFileSync(key) })
    : http2.createServer();
server.on("stream", (stream, headers) => {
  stream.respond({ ":status": 200, "content-type": "text/plain" });
  stream.end("hello " + headers[":path"]);
});
server.listen(0, "127.0.0.1", () => {
  const temporary = portFile + ".tmp";
  fs.writeFileSync(temporary, String(server.address().port));
  fs.renameSync(temporary, portFile);
});
`;

const NODE_CLIENT = `
const http2 = require("node:http2");
const [scheme, port] = process.argv.slice(2);
const session = http2.connect(scheme + "://localhost:" + port, { rejectUnauthorized: false });
session.on("error", (error) => {
  console.error("session", error.message);
  process.exit(1);
});
const request = session.request({ ":path": "/x" });
request.setEncoding("utf8");
request.on("response", (headers) => console.log("status", headers[":status"]));
request.on("data", (data) => console.log("data", data));
request.on("end", () => {
  console.log("end");
  session.close();
  setTimeout(() => process.exit(0), 50);
});
request.end();
`;

const describeWithHttp2 = hasClang() && http2Available ? describe : describe.skip;

describeWithHttp2("http2", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-http2-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function buildProgram(source: string): string {
    const entry = join(workdir, `program_${Math.random().toString(36).slice(2)}.ts`);
    writeFileSync(entry, source);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir,
      extensions: createDefaultRegistry().register(nodeExtension),
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    return result.outputPath;
  }

  function clientSource(scheme: string, port: number): string {
    return `
      import { connect, constants } from "node:http2";

      const session = connect("${scheme}://localhost:${port}");
      const request = session.request({ ":path": "/h2" });
      request.setEncoding("utf8");
      request.on("response", (headers: Record<string, string>) => {
        console.log("status", headers[":status"]);
        console.log("constants", constants.NGHTTP2_FLAG_END_STREAM, constants.NGHTTP2_CANCEL);
      });
      request.on("data", (data: string) => console.log("data", data));
      request.on("end", () => {
        console.log("end");
        session.close();
      });
      request.end();
    `;
  }

  async function startNodeHttp2(mode: "plain" | "secure") {
    const script = join(workdir, `node-h2-${mode}.cjs`);
    writeFileSync(script, NODE_SERVER);
    const portFile = join(workdir, `node-h2-${mode}.txt`);
    const server = await startNodeServer(script, [mode, certificatePath, keyPath], portFile);
    return server;
  }

  it("client talks h2c to a Node server", async () => {
    const server = await startNodeHttp2("plain");
    try {
      const binary = buildProgram(clientSource("http", server.port));
      const executed = runBinary(binary, []);
      expect(executed.status).toBe(0);
      expect(executed.stdout.trim().split("\n")).toEqual([
        "status 200",
        "constants 1 8",
        "data hello /h2",
        "end",
      ]);
    } finally {
      server.child.kill();
    }
  });

  it("client talks HTTP/2 over TLS to a Node server", async () => {
    const server = await startNodeHttp2("secure");
    try {
      const binary = buildProgram(clientSource("https", server.port));
      const executed = runBinary(binary, [], { NODE_EXTRA_CA_CERTS: certificatePath });
      expect(executed.status, executed.stderr).toBe(0);
      expect(executed.stdout.trim().split("\n")).toEqual([
        "status 200",
        "constants 1 8",
        "data hello /h2",
        "end",
      ]);
    } finally {
      server.child.kill();
    }
  });

  function serverSource(secure: boolean): string {
    const setup = secure
      ? `import { readFileSync } from "node:fs";
         const server = createSecureServer({
           cert: readFileSync(process.argv[2], "utf8"),
           key: readFileSync(process.argv[3], "utf8"),
         });`
      : `const server = createServer();`;
    return `
      import { createServer, createSecureServer } from "node:http2";
      import { renameSync, writeFileSync } from "node:fs";
      ${setup}
      server.on("stream", (stream, headers: Record<string, string>) => {
        stream.respond({ ":status": 201, "content-type": "text/plain" });
        stream.write("served ");
        stream.end(headers[":path"]);
      });
      server.listen(0, "127.0.0.1", () => {
        const address = server.address();
        if (address) {
          const portFile = process.argv[${secure ? 4 : 2}];
          const temporary = portFile + ".tmp";
          writeFileSync(temporary, String(address.port));
          renameSync(temporary, portFile);
        }
      });
    `;
  }

  it("compiled h2c server answers a Node client", async () => {
    const binary = buildProgram(serverSource(false));
    const portFile = join(workdir, "compiled-h2c.txt");
    const server = await startBinaryServer(binary, [], portFile);
    try {
      const client = join(workdir, "node-h2-client.cjs");
      writeFileSync(client, NODE_CLIENT);
      const executed = runNode(client, ["http", String(server.port)]);
      expect(executed.status).toBe(0);
      expect(executed.stdout.trim().split("\n")).toEqual(["status 201", "data served /x", "end"]);
    } finally {
      server.child.kill();
    }
  });

  it("compiled secure server answers a Node client", async () => {
    const binary = buildProgram(serverSource(true));
    const portFile = join(workdir, "compiled-h2.txt");
    const server = await startBinaryServer(binary, [certificatePath, keyPath], portFile);
    try {
      const client = join(workdir, "node-h2-client-secure.cjs");
      writeFileSync(client, NODE_CLIENT);
      const executed = runNode(client, ["https", String(server.port)]);
      expect(executed.status).toBe(0);
      expect(executed.stdout.trim().split("\n")).toEqual(["status 201", "data served /x", "end"]);
    } finally {
      server.child.kill();
    }
  });
});
