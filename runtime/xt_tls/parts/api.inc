/* -- API table and registration ------------------------------------------- */

static const XtTlsApi kTlsApi = {
    xt_tls_client_connect,
    xt_tls_server_config_new,
    xt_tls_server_accept,
    xt_tls_conn_read,
    xt_tls_conn_write,
    xt_tls_conn_shutdown,
    xt_tls_conn_close,
    xt_tls_conn_free,
    xt_tls_server_config_free,
    xt_tls_conn_alpn,
    xt_tls_conn_error,
    xt_tls_conn_fd,
};

/* Called by `tls_bridge.c`, which is the feature's compiled `runtimeSources()`
 * entry. The call is what makes the linker extract this object (and the mbedTLS
 * members it references) from `tls.lib`. */
void xt_tls_backend_boot(void) { xt_tls_core_register(&kTlsApi); }
