/*
 * xbintsc GUI engine — `gpu` link feature implementation.
 *
 * This translation unit is the only place in `gui.a` that references glslang and
 * SPIRV-Cross. It is pulled out of the archive only when `gpu_bridge.c` (the
 * feature's C registration object) is linked, which happens only when the
 * compiler detects that the program may compile shaders. Without it the engine
 * sees a null table (`gpu_shader.cpp`) and no glslang member is extracted.
 */

#include "gpu_shader_api.h"
#include "webgl_msl.h"

namespace {

const xtgui::GpuShaderApi kGpuShaderApi = {
    xtgui::compile_glsl,
    xtgui::reflect_uniform_block,
    xtgui::reflect_attributes,
    xtgui::cross_compile_msl,
};

}  // namespace

extern "C" void xt_gpu_feature_register(void) {
  xtgui::gpu_shader_install(&kGpuShaderApi);
}
