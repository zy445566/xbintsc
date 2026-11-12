/**
 * The `node-tls` link feature: the JavaScript surface of `node:tls`.
 *
 * The always-linked `tls` module keeps the importable bindings and reports a
 * clear runtime error when the feature is absent. When a program imports
 * `node:tls`, this feature compiles the backend that builds `TLSSocket`
 * objects on top of the `tls` feature's mbedTLS transport.
 */

import type { LinkFeature, ProgramUsage } from "../../link-feature.js";
import { resolveFrom } from "../module.js";

/** True when the bundled program imports `tls` or names its main class. */
export function mentionsNodeTls(usage: ProgramUsage): boolean {
  return (
    usage.strings.includes("tls") ||
    usage.strings.includes("node:tls") ||
    usage.identifiers.has("TLSSocket") ||
    usage.identifiers.has("createSecureServer")
  );
}

export const nodeTlsFeature: LinkFeature = {
  name: "node-tls",
  description: "node:tls TLSSocket/client/server on top of mbedTLS",
  requires: ["tls"],
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/tls/tls_backend.c")],
  used: mentionsNodeTls,
};
