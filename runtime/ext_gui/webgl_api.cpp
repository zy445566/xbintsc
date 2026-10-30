/*
 * xbintsc GUI engine — core-side registry for the optional WebGL table.
 *
 * Kept in `gui.a` and free of any `webgl.h`/SDL type: the table is installed by
 * the `webgl` feature's bridge when that feature is linked, and stays null
 * otherwise. See `webgl_api.h`.
 */

#include "webgl_api.h"

namespace {

const XtWebglApi *g_api = nullptr;

}  // namespace

void xt_webgl_api_install(const XtWebglApi *api) { g_api = api; }

const XtWebglApi *xt_webgl_api() { return g_api; }
