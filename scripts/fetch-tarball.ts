#!/usr/bin/env node
/**
 * Fetch a pinned release tarball for the vendored toolchain.
 *
 * `scripts/build-media.ts` (FFmpeg) and `scripts/build-gui.ts` (SDL3, FreeType,
 * HarfBuzz, glslang, SPIRV-Cross) build from release tarballs downloaded at
 * build time. A single `curl` is not reliable enough for that: hosted CI
 * runners and some networks intermittently fail the TLS handshake with the
 * release hosts — `curl: (35) SSL connect error` — and the next attempt
 * usually succeeds. CI also caches the tarball by version, so with a retry here
 * the download happens at most once per release rather than once per run.
 *
 * The bytes are written to `<destination>.part` and renamed only after curl
 * reports success: an interrupted transfer can then never be mistaken for a
 * complete tarball, neither by this run nor by a later one that restores the
 * directory from `actions/cache`.
 */

import { spawnSync } from "node:child_process";
import { renameSync, rmSync } from "node:fs";

/** curl exit codes of transfers that may succeed when repeated: name
 * resolution (6), connect timeout (28), TLS handshake (35), empty reply (52),
 * send failure (55) and connection reset (56). Everything else — a 404, a
 * permission error — is reported immediately. */
const TRANSIENT_STATUSES = new Set([6, 28, 35, 52, 55, 56]);

export interface DownloadOptions {
  /** Total attempts, including the first. */
  attempts?: number;
  /** Base backoff in milliseconds; attempt n waits `retryDelayMs * n`. */
  retryDelayMs?: number;
}

/** Block the single-threaded build long enough for a fresh TLS handshake. */
function pause(ms: number): void {
  if (ms <= 0) return;
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
}

/**
 * Download `url` to `destination`, retrying the transient failures above.
 * Throws when the transfer still fails, leaving no `.part` file behind.
 */
export function downloadTarball(
  destination: string,
  url: string,
  label: string,
  options: DownloadOptions = {},
): void {
  const attempts = options.attempts ?? 6;
  const retryDelayMs = options.retryDelayMs ?? 3_000;
  const partial = `${destination}.part`;
  rmSync(partial, { force: true });
  for (let attempt = 1; ; attempt += 1) {
    const note = attempt === 1 ? "" : ` (attempt ${attempt}/${attempts})`;
    console.log(`xbintsc: fetching ${label} from ${url}${note}`);
    const { status } = spawnSync("curl", ["-sL", "--fail", "--connect-timeout", "30", "-o", partial, url], {
      stdio: ["ignore", "inherit", "inherit"],
    });
    if (status === 0) {
      renameSync(partial, destination);
      return;
    }
    if (status === null || !TRANSIENT_STATUSES.has(status) || attempt >= attempts) {
      rmSync(partial, { force: true });
      throw new Error(`${label}: curl ${url} failed with status ${status ?? "?"}`);
    }
    pause(retryDelayMs * attempt);
  }
}
