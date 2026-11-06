/*
 * xbintsc GUI engine — WGSL front end for WebGPU.
 *
 * WebGPU programs supply WGSL at run time. The engine has exactly one shader
 * compiler (glslang, behind the optional `gpu` link feature) and it consumes
 * GLSL, so this module translates the WGSL subset WebGPU programs use into
 * Vulkan-dialect GLSL ES, and then reuses the WebGL path: glslang produces
 * SPIR-V for Vulkan and SPIRV-Cross produces MSL for Metal.
 *
 * A translation is requested per *entry point*, because a WGSL module may hold
 * the vertex and fragment stages (and several entry points) at once while a
 * graphics pipeline consumes one pair. The result also reports where the
 * module's resources live, in the slot order SDL_GPU binds them, so the
 * pipeline can bind a WebGPU bind group without re-reflecting the SPIR-V.
 */
#ifndef XT_GUI_WGSL_H
#define XT_GUI_WGSL_H

#include <cstdint>
#include <string>
#include <vector>

namespace xtgui {
namespace wgsl {

enum class Stage { Vertex = 0, Fragment = 1, Compute = 2 };

/** A resource reference as it appeared in WGSL: `@group(g) @binding(b)`. */
struct Binding {
  int group = 0;
  int binding = 0;
  bool operator==(const Binding &other) const {
    return group == other.group && binding == other.binding;
  }
};

/**
 * One SDL_GPU sampler slot. WGSL declares textures and samplers separately,
 * but SDL_GPU binds them as pairs, so the translator pairs every texture with
 * each sampler it is sampled with (`textureSample(t, s, …)`). `has_sampler` is
 * false for `textureLoad`-only textures, which SDL still needs a sampler for;
 * the runtime supplies a default one.
 */
struct TextureSlot {
  Binding texture;
  bool has_sampler = false;
  Binding sampler;
};

/** The translated GLSL for one entry point plus the bindings it uses. */
struct StageCode {
  std::string glsl;
  /** Name of the GLSL entry point (`main`). */
  std::string entry = "main";

  /** SDL sampler slots, in binding order. */
  std::vector<TextureSlot> samplers;
  /** Read-only storage textures, in SDL binding order. */
  std::vector<Binding> storage_textures;
  /** Read-only storage buffers, in SDL binding order. */
  std::vector<Binding> storage_buffers;
  /** Write storage textures (compute only). */
  std::vector<Binding> write_storage_textures;
  /** Read-write storage buffers (compute only). */
  std::vector<Binding> write_storage_buffers;
  /** Uniform buffers, in SDL binding order (at most 4 per stage). */
  std::vector<Binding> uniform_buffers;

  /** WGSL `@workgroup_size`, for compute pipelines; 0 for other stages. */
  int workgroup_size[3] = {0, 0, 0};
};

/** Set by a failed translation; the text is shown by `getCompilationInfo`. */
struct CompileInfo {
  std::string message;
  int line = 0;
  int column = 0;
};

/**
 * Translate `entry_point` of a WGSL `source` for `stage` into GLSL.
 *
 * Returns false and fills `info` when the source does not parse, the entry
 * point is missing, or it is declared for another stage. The caller reports
 * the message through `GPUCompilationMessage`.
 */
bool translate(const std::string &source, const std::string &entry_point, Stage stage,
               StageCode *out, CompileInfo *info);

/**
 * Check that `source` parses at all (used by `getCompilationInfo`); every
 * entry point is validated but nothing is emitted.
 */
bool check(const std::string &source, CompileInfo *info);

}  // namespace wgsl
}  // namespace xtgui

#endif /* XT_GUI_WGSL_H */
