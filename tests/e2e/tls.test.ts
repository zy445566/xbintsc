/**
 * End-to-end coverage for TLS: `node:tls` client and server, and the global
 * `fetch` over `https://`.
 *
 * The peer is always a Node process (a TLS echo server, an HTTPS server, or a
 * Node client for a compiled server), so these tests prove interoperability
 * rather than only internal consistency. They need the `tls` archive built by
 * `npm run tls`; a checkout without it skips the suite.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
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
const tlsAvailable = findRuntimeLibrary("tls") !== undefined;

const TLS_ECHO_SERVER = `
const tls = require("node:tls");
const fs = require("node:fs");
const [cert, key, portFile] = process.argv.slice(2);
const server = tls.createServer({ cert: fs.readFileSync(cert), key: fs.readFileSync(key) }, (socket) => {
  socket.setEncoding("utf8");
  socket.on("data", (data) => socket.write("echo:" + data));
  socket.on("end", () => socket.end());
});
server.listen(0, "127.0.0.1", () => {
  const temporary = portFile + ".tmp";
  fs.writeFileSync(temporary, String(server.address().port));
  fs.renameSync(temporary, portFile);
});
`;

const HTTPS_SERVER = `
const https = require("node:https");
const fs = require("node:fs");
const [cert, key, portFile] = process.argv.slice(2);
const server = https.createServer(
  { cert: fs.readFileSync(cert), key: fs.readFileSync(key) },
  (req, res) => {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({ secure: true, path: req.url }));
  },
);
server.listen(0, "127.0.0.1", () => {
  const temporary = portFile + ".tmp";
  fs.writeFileSync(temporary, String(server.address().port));
  fs.renameSync(temporary, portFile);
});
`;

const NODE_TLS_CLIENT = `
const tls = require("node:tls");
const port = Number(process.argv[2]);
const socket = tls.connect({ port, host: "127.0.0.1", rejectUnauthorized: false }, () => {
  socket.write("hello");
});
socket.setEncoding("utf8");
socket.on("data", (data) => {
  console.log("node got", data);
  socket.end();
});
socket.on("close", () => process.exit(0));
`;

const describeWithTls = hasClang() && tlsAvailable ? describe : describe.skip;

describeWithTls("tls and https", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-tls-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function buildProgram(source: string, withNode = true): string {
    const entry = join(workdir, `program_${Math.random().toString(36).slice(2)}.ts`);
    writeFileSync(entry, source);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir,
      ...(withNode ? { extensions: createDefaultRegistry().register(nodeExtension) } : {}),
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    return result.outputPath;
  }

  async function startTlsServer(): Promise<{ portFile: string; port: number; kill: () => void }> {
    const script = join(workdir, `tls-server-${Math.random().toString(36).slice(2)}.cjs`);
    writeFileSync(script, TLS_ECHO_SERVER);
    const portFile = join(workdir, `tls-port-${Math.random().toString(36).slice(2)}.txt`);
    const server = await startNodeServer(script, [certificatePath, keyPath], portFile);
    return { portFile, port: server.port, kill: () => server.child.kill() };
  }

  it("connects to a Node TLS server, verifies the CA and exchanges data", async () => {
    const server = await startTlsServer();
    try {
      const binary = buildProgram(`
        import { connect } from "node:tls";
        import { readFileSync } from "node:fs";

        const certificate = readFileSync(process.argv[2], "utf8");
        const socket = connect(
          { port: ${server.port}, host: "127.0.0.1", servername: "localhost", ca: certificate },
          () => socket.write("hello"),
        );
        socket.setEncoding("utf8");
        socket.on("data", (data: string) => {
          console.log("secure", socket.authorized, socket.encrypted);
          console.log("data", data);
          socket.destroy();
        });
        socket.on("close", () => console.log("closed"));
      `);
      const executed = runBinary(binary, [certificatePath]);
      expect(executed.status).toBe(0);
      expect(executed.stdout.trim().split("\n")).toEqual(["secure true true", "data echo:hello", "closed"]);
    } finally {
      server.kill();
    }
  });

  it("serves TLS to a Node client", async () => {
    const binary = buildProgram(`
      import { createServer } from "node:tls";
      import { readFileSync, renameSync, writeFileSync } from "node:fs";

      const server = createServer(
        { cert: readFileSync(process.argv[2], "utf8"), key: readFileSync(process.argv[3], "utf8") },
        (socket) => {
          socket.setEncoding("utf8");
          socket.on("data", (data: string) => socket.write("echo:" + data));
        },
      );
      server.listen(0, "127.0.0.1", () => {
        const address = server.address();
        if (address) {
          const temporary = process.argv[4] + ".tmp";
          writeFileSync(temporary, String(address.port));
          renameSync(temporary, process.argv[4]);
        }
      });
    `);
    const portFile = join(workdir, `tls-server-port-${Math.random().toString(36).slice(2)}.txt`);
    const server = await startBinaryServer(binary, [certificatePath, keyPath], portFile);
    try {
      const client = join(workdir, "node-tls-client.cjs");
      writeFileSync(client, NODE_TLS_CLIENT);
      const executed = runNode(client, [String(server.port)]);
      expect(executed.status).toBe(0);
      expect(executed.stdout.trim()).toBe("node got echo:hello");
    } finally {
      server.child.kill();
    }
  });

  it("fetches https URLs when the CA is trusted and rejects an unknown CA", async () => {
    const script = join(workdir, "https-server.cjs");
    writeFileSync(script, HTTPS_SERVER);
    const portFile = join(workdir, "https-port.txt");
    const server = await startNodeServer(script, [certificatePath, keyPath], portFile);
    try {
      const binary = buildProgram(
        `
        async function main() {
          try {
            const response = await fetch("https://localhost:${server.port}/secure");
            console.log(response.status, response.headers.get("content-type"));
            console.log(JSON.stringify(await response.json()));
          } catch (error) {
            console.log("rejected", error instanceof TypeError);
          }
        }
        main();
      `,
        false,
      );

      const trusted = runBinary(binary, [], {
        NODE_EXTRA_CA_CERTS: resolve(certificatePath),
      });
      expect(trusted.status).toBe(0);
      expect(trusted.stdout.trim().split("\n")).toEqual([
        "200 application/json",
        '{"secure":true,"path":"/secure"}',
      ]);

      const untrusted = runBinary(binary, [], { NODE_EXTRA_CA_CERTS: "" });
      expect(untrusted.status).toBe(0);
      expect(untrusted.stdout.trim()).toBe("rejected true");
    } finally {
      server.child.kill();
    }
  });

});
