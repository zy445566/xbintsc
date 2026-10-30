/*
 * xbintsc GUI engine — optional GPU shader translation table.
 *
 * WebGL shaders and the `mix-blend-mode` compositor both need GLSL→SPIR-V
 * translation (glslang), and on Metal a second SPIR-V→MSL pass (SPIRV-Cross).
 * Those are the largest optional third-party dependencies in the GUI engine, so
 * the engine reaches them through this table instead of linking them directly.
 *
 * The implementation lives in `gpu_feature.cpp` and is only pulled out of
 * `gui.a` when the `gpu` link feature's bridge is linked. Without it the table is
 * null: the blend compositor falls back to ordinary painting and WebGL reports a
 * shader compile log, but a DOM/2D-canvas page still runs.
 */
#ifndef XT_GUI_GPU_SHADER_API_H
#define XT_GUI_GPU_SHADER_API_H

#include <string>
#include <unordered_map>
#include <vector>

#include "webgl_glsl.h"

namespace xtgui {

/** The four translation/reflection entry points the engine calls on demand. */
struct GpuShaderApi {
  bool (*compile_glsl)(const std::string &source, bool fragment,
                       std::vector<uint32_t> *spirv, std::string *log);
  bool (*reflect_uniform_block)(const std::vector<uint32_t> &spirv,
                                std::vector<GlslBlockMember> *out);
  bool (*reflect_attributes)(const std::vector<uint32_t> &spirv,
                             std::unordered_map<std::string, int> *out);
  bool (*cross_compile_msl)(const std::vector<uint32_t> &spirv, bool fragment,
                            std::string *source, std::string *entry, std::string *log);
};

/** Install the table. Called by the `gpu` feature bridge before `main`. */
void gpu_shader_install(const GpuShaderApi *api);

/** The installed table, or null when the `gpu` feature was not linked. */
const GpuShaderApi *gpu_shader_api();

}  // namespace xtgui

#endif /* XT_GUI_GPU_SHADER_API_H */
