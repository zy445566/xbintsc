/*
 * xbintsc GUI engine — core-side registry for the optional image/SVG table.
 *
 * Kept in `gui.a`, free of stb_image/NanoSVG: the table is installed by the
 * `images` feature bridge when that feature is linked, and stays null otherwise.
 */

#include "image_api.h"

namespace {

const XtImageApi *g_api = nullptr;

}  // namespace

void xt_image_api_install(const XtImageApi *api) { g_api = api; }

const XtImageApi *xt_image_api() { return g_api; }
