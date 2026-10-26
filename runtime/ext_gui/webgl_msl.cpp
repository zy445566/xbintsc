/*
 * xbintsc GUI engine — SPIRV-Cross integration (SPIR-V -> MSL).
 *
 * `cross_compile_msl` is the only place that calls into SPIRV-Cross. It runs
 * after `compile_glsl`, on the module glslang produced, so both GPU backends
 * share one GLSL front end: the Vulkan path hands the SPIR-V to SDL_GPU
 * directly, the Metal path cross-compiles it to MSL text that SDL_GPU's Metal
 * backend compiles at run time.
 */

#include "webgl_msl.h"

#include <exception>

#include <spirv_msl.hpp>

namespace xtgui {

bool cross_compile_msl(const std::vector<uint32_t> &spirv, bool fragment,
                       std::string *source, std::string *entry, std::string *log) {
  if (source == nullptr || entry == nullptr || log == nullptr) return false;
  source->clear();
  entry->clear();
  log->clear();
  if (spirv.empty()) {
    *log = "empty SPIR-V module";
    return false;
  }
  try {
    spirv_cross::CompilerMSL compiler(spirv);
    spirv_cross::CompilerMSL::Options options = compiler.get_msl_options();
    /* Reuse the SPIR-V bindings instead of letting SPIRV-Cross allocate MSL
     * indices on demand. The rewriter gives the uniform block binding 0 and
     * the N-th sampler binding N; SDL_GPU's Metal backend pushes the block at
     * buffer 0 and binds both the texture and sampler arrays over
     * 0..num_samplers-1, so any other allocation would sample the wrong
     * texture (or none at all). */
    options.enable_decoration_binding = true;
    compiler.set_msl_options(options);
    *source = compiler.compile();
    *entry = compiler.get_cleansed_entry_point_name(
        "main", fragment ? spv::ExecutionModelFragment : spv::ExecutionModelVertex);
    if (entry->empty()) {
      *log = "MSL entry point vanished during translation";
      source->clear();
      return false;
    }
    return true;
  } catch (const std::exception &error) {
    *log = error.what();
    source->clear();
    entry->clear();
    return false;
  } catch (...) {
    *log = "SPIRV-Cross failed with an unknown error";
    source->clear();
    entry->clear();
    return false;
  }
}

}  // namespace xtgui
