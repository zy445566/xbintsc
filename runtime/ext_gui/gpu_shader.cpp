/*
 * xbintsc GUI engine — core-side registry for the optional GPU shader table.
 *
 * Kept in `gui.a` and free of any glslang/SPIRV-Cross dependency: the table is
 * installed by the `gpu` feature's bridge when that feature is linked, and stays
 * null otherwise. See `gpu_shader_api.h`.
 */

#include "gpu_shader_api.h"

namespace xtgui {
namespace {

const GpuShaderApi *g_api = nullptr;

}  // namespace

void gpu_shader_install(const GpuShaderApi *api) { g_api = api; }

const GpuShaderApi *gpu_shader_api() { return g_api; }

}  // namespace xtgui
