/**
 * The `tls` link feature: mbedTLS, used by `fetch` for `https://` URLs.
 *
 * TLS is heavy (~200 KiB of mbedTLS code) and most programs never make an
 * HTTPS request, so it is an optional feature of the always-on `core`
 * extension. The driver scans the bundled program for an `https://` literal and
 * only then compiles `runtime/xt_tls/tls_bridge.c` and links the mbedTLS
 * archive (which contains `tls_core.c`); the always-linked runtime reaches the
 * backend through the `xt_tls_core_api` dispatch table, which stays NULL
 * otherwise.
 *
 * `node:tls` and `node:http2` declare their own features that `requires` this
 * one, and `--feature tls` forces it for a URL the scan cannot see. The archive
 * is produced by `npm run tls`; without it the feature is reported unavailable
 * and HTTPS requests reject with a clear runtime error.
 */

import { resolve } from "node:path";
import { findRuntimeDir } from "../driver/paths.js";
import { findRuntimeLibrary } from "../driver/runtime-lib.js";
import type { LinkFeature, ProgramUsage } from "./link-feature.js";

/**
 * Resolve a prebuilt third-party archive, or throw with the command that builds
 * it. `resolveLinkFeatures` turns the throw into a warning for an auto-detected
 * feature and into an error for an explicitly forced one.
 */
export function requireRuntimeLibrary(name: string, buildCommand: string): string {
  const archive = findRuntimeLibrary(name);
  if (archive === undefined) {
    throw new Error(
      `the '${name}' archive is not built; run \`${buildCommand}\` to fetch and build it`,
    );
  }
  return archive;
}

/** True when a string literal names an HTTPS URL. */
export function mentionsHttps(usage: ProgramUsage): boolean {
  return usage.strings.some((text) => /^https:\/\//i.test(text));
}

export const tlsFeature: LinkFeature = {
  name: "tls",
  description: "mbedTLS TLS transport used by fetch() for https:// URLs and node:tls",
  runtimeSources: () => [resolve(findRuntimeDir(), "xt_tls", "tls_bridge.c")],
  nativeObjects: () => [requireRuntimeLibrary("tls", "npm run tls")],
  // Windows reads its root certificate store through crypt32; the Win32 socket
  // and bcrypt libraries are already linked by the driver.
  linkerFlags: () => (process.platform === "win32" ? ["-lcrypt32"] : []),
  used: (usage: ProgramUsage): boolean => mentionsHttps(usage) || usage.identifiers.has("TLSSocket"),
};
