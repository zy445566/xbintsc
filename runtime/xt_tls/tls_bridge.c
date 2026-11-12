/*
 * xbintsc `tls` link feature — registration bridge.
 *
 * The mbedTLS transport (and `tls_core.c`) lives in `tls.lib`, which is linked
 * only when the feature is active. A static archive member with just a
 * constructor is never extracted, so this tiny translation unit — the
 * feature's `runtimeSources()` entry — references `xt_tls_backend_boot()`.
 * That call pulls the implementation (and the mbedTLS members it uses) out of
 * the archive and installs the dispatch table before `main`.
 *
 * It intentionally includes no mbedTLS header: the npm package ships the
 * archive, not the vendored headers, so a program that enables TLS must be
 * compilable without `vendor/`.
 */

extern void xt_tls_backend_boot(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_tls_bridge_boot(void) {
  xt_tls_backend_boot();
}
#elif defined(_MSC_VER)
void xt_tls_bridge_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_tls_bridge_boot_ctor)(void) = xt_tls_bridge_boot;
void xt_tls_bridge_boot(void) { xt_tls_backend_boot(); }
#endif
