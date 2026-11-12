/*
 * xbintsc `tls` link feature — the mbedTLS transport.
 *
 * This translation unit is compiled only when a program may use TLS (an
 * `https://` URL, `node:tls`, or an explicit `--feature tls`). It installs the
 * `XtTlsApi` table into the always-linked core registry from a constructor, so
 * `fetch` and `node:tls` can fall back to a clear runtime error when the
 * feature is not linked instead of failing at link time.
 *
 * The mbedTLS implementation itself lives in the `tls` archive
 * (`runtime/lib/<os>-<arch>/tls.{a,lib}`), built by `npm run tls`. This file
 * is the feature's `runtimeSources()` entry: linking it directly is what pulls
 * the archive members it references out of the static library.
 */

#if defined(_WIN32)
/* winsock2.h must precede windows.h; the configuration code below includes the
 * latter for the certificate store API. */
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "../xt_tls_api.h"
#include "tls_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/error.h"

static int xt_tls_finish_handshake(xt_tls_conn *conn, int verify);
static void xt_tls_conn_close(xt_tls_conn *conn);

#include "parts/setup.inc"
#include "parts/io.inc"
#include "parts/client.inc"
#include "parts/server.inc"
#include "parts/api.inc"
