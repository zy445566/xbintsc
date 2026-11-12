/**
 * The `http2` link feature: nghttp2-backed `node:http2`.
 *
 * HTTP/2 framing and HPACK come from the vendored nghttp2 archive; TLS (and
 * therefore ALPN) comes from the `tls` feature, and the JavaScript object
 * surface reuses `node-tls`'s server wrapper for `createSecureServer`. The
 * always-linked `http2` module forwards to the dispatch table this feature
 * installs, so a checkout without the archive still compiles and reports a
 * clear runtime error instead of failing to link.
 */

import type { LinkFeature, ProgramUsage } from "../../link-feature.js";
import { resolveFrom } from "../module.js";
import { requireRuntimeLibrary } from "../../tls.js";

/** True when the bundled program imports `http2` or uses its server API. */
export function mentionsHttp2(usage: ProgramUsage): boolean {
  return (
    usage.strings.includes("http2") ||
    usage.strings.includes("node:http2") ||
    usage.identifiers.has("createSecureServer") ||
    usage.identifiers.has("Http2Session")
  );
}

export const http2Feature: LinkFeature = {
  name: "http2",
  description: "node:http2 client and server using nghttp2 over TLS or cleartext",
  requires: ["tls", "node-tls"],
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/http2/http2_bridge.c")],
  nativeObjects: () => [requireRuntimeLibrary("nghttp2", "npm run tls")],
  used: mentionsHttp2,
};
