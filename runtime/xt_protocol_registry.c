/*
 * Registration slots for the optional TLS and HTTP/2 backends.
 *
 * These globals live in the core runtime so a program that never enables the
 * `tls`/`http2` link features still links: the fallbacks (rejecting `https://`,
 * throwing from `node:tls`) only need the NULL check. The feature
 * implementations install their tables from a constructor before `main`.
 */

#include "xt_http2_api.h"
#include "xt_node_tls_api.h"
#include "xt_tls_api.h"

const XtTlsApi *xt_tls_core_api = NULL;
const XtNodeTlsApi *xt_tls_node_api = NULL;
const XtHttp2Api *xt_http2_api = NULL;

void xt_tls_core_register(const XtTlsApi *api) { xt_tls_core_api = api; }

void xt_node_tls_register(const XtNodeTlsApi *api) { xt_tls_node_api = api; }

void xt_http2_register(const XtHttp2Api *api) { xt_http2_api = api; }
