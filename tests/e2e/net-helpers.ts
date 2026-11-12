/**
 * Helpers for end-to-end suites that run a Node server in a child process.
 *
 * The compiled program is executed synchronously (`spawnSync`), which would
 * starve an in-process server of event-loop turns, so the peer always lives in
 * a child. The child publishes its ephemeral port by writing the number into a
 * file; the test polls until the file contains a complete port.
 */

import { existsSync, readFileSync } from "node:fs";
import { spawn, spawnSync, type ChildProcess } from "node:child_process";

/** Wait until `file` holds a decimal port, then return it. */
export async function waitForPort(file: string, timeoutMs = 30_000): Promise<number> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (existsSync(file)) {
      const text = readFileSync(file, "utf8").trim();
      if (/^\d+$/.test(text)) {
        const port = Number(text);
        if (port > 0 && port < 65536) return port;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  throw new Error(`the server did not publish a port in ${file}`);
}

export interface NodeServer {
  readonly child: ChildProcess;
  readonly port: number;
}

/** Start `script` under Node and wait for it to publish its port. */
export async function startNodeServer(
  script: string,
  args: readonly string[],
  portFile: string,
): Promise<NodeServer> {
  const child = spawn(process.execPath, [script, ...args, portFile], {
    stdio: ["ignore", "ignore", "inherit"],
  });
  const port = await waitForPort(portFile);
  return { child, port };
}

/** Start a compiled binary and wait for it to publish its port. */
export async function startBinaryServer(
  binary: string,
  args: readonly string[],
  portFile: string,
  env?: Record<string, string>,
): Promise<NodeServer> {
  const child = spawn(binary, [...args, portFile], {
    stdio: ["ignore", "ignore", "inherit"],
    ...(env ? { env: { ...process.env, ...env } } : {}),
  });
  const port = await waitForPort(portFile);
  return { child, port };
}

export interface Executed {
  readonly stdout: string;
  readonly stderr: string;
  readonly status: number | null;
}

/** Run a binary to completion, capturing its output. */
export function runBinary(
  binary: string,
  args: readonly string[] = [],
  env?: Record<string, string>,
  timeout = 60_000,
): Executed {
  const result = spawnSync(binary, [...args], {
    encoding: "utf8",
    timeout,
    ...(env ? { env: { ...process.env, ...env } } : {}),
  });
  if (result.error) throw result.error;
  return { stdout: result.stdout ?? "", stderr: result.stderr ?? "", status: result.status };
}

/** Run a Node script to completion, capturing its output. */
export function runNode(
  script: string,
  args: readonly string[] = [],
  timeout = 60_000,
): Executed {
  return runBinary(process.execPath, [script, ...args], undefined, timeout);
}
