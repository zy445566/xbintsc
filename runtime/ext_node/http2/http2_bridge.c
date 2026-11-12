/*
 * xbintsc `http2` link feature — registration bridge.
 *
 * The nghttp2-backed implementation (and `http2_backend.c`) is archived in
 * `nghttp2.lib`. This direct-linked unit references `xt_http2_backend_boot()`,
 * which forces the linker to extract the backend and the nghttp2 members it
 * uses, then installs the dispatch table before `main`.
 *
 * No nghttp2 header is included here: the npm package ships the archive, not
 * the vendored headers, so the bridge must compile without `vendor/`.
 */

extern void xt_http2_backend_boot(void);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void xt_http2_bridge_boot(void) {
  xt_http2_backend_boot();
}
#elif defined(_MSC_VER)
void xt_http2_bridge_boot(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*const xt_http2_bridge_boot_ctor)(void) =
    xt_http2_bridge_boot;
void xt_http2_bridge_boot(void) { xt_http2_backend_boot(); }
#endif
