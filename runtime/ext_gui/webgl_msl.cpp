/*
 * xbintsc GUI engine — SPIRV-Cross integration (SPIR-V -> MSL).
 *
 * `cross_compile_msl` is the only place that calls into SPIRV-Cross. It runs
 * after `compile_glsl`, on the module glslang produced, so both GPU backends
 * share one GLSL front end: the Vulkan path hands the SPIR-V to SDL_GPU
 * directly, the Metal path cross-compiles it to MSL text that SDL_GPU's Metal
 * backend compiles at run time.
 *
 * Metal is the only SDL_GPU backend that consumes MSL, so `scripts/build-gui.ts`
 * merges SPIRV-Cross into `gui.a` on macOS only; the rest of this file is a stub
 * (the `#else` branch) so no caller needs an `#if`. Including the archive
 * everywhere would instead add ~2.5 MiB of code no driver can ever ask for.
 */

#include "webgl_msl.h"

#if defined(__APPLE__)

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
    /* Sampler arrays (three.js shadow maps) need MSL 2.0: with the default
     * 1.2 target SPIRV-Cross rejects "MSL 2.0 or greater is required for
     * arrays of textures". Every Metal device SDL_GPU runs on supports 2.0. */
    options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 0);
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

#else /* !__APPLE__ */

namespace xtgui {

/**
 * Non-Metal builds carry no SPIRV-Cross, so there is nothing to translate with.
 *
 * No SDL_GPU backend outside Apple platforms reports MSL, so no driver can reach
 * this; it exists so the callers keep one unconditional branch and a device that
 * ever did advertise MSL fails with an actionable log instead of a wrong shader.
 */
bool cross_compile_msl(const std::vector<uint32_t> &spirv, bool fragment,
                       std::string *source, std::string *entry, std::string *log) {
  (void)spirv;
  (void)fragment;
  source->clear();
  entry->clear();
  *log = "this build has no MSL backend (SPIRV-Cross is macOS-only)";
  return false;
}

}  // namespace xtgui

#endif /* __APPLE__ */
