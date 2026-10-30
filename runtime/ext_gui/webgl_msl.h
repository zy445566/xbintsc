/*
 * xbintsc GUI engine — Metal Shading Language for WebGL.
 *
 * SDL_GPU's Metal backend has no shader compiler either, but unlike Vulkan's
 * SPIR-V it consumes MSL *text* and compiles it at run time. WebGL's GLSL is
 * already translated to SPIR-V for the Vulkan path, so the Metal path reuses
 * that module instead of growing a second GLSL front end: SPIRV-Cross
 * cross-compiles it to MSL while keeping the binding indices the rewriter
 * assigned — one std140 block at buffer 0 and stage-local samplers at
 * binding N, which is exactly the slot layout SDL_GPU's Metal backend binds.
 *
 * Only a macOS build links SPIRV-Cross (`scripts/build-gui.ts` skips the
 * archive elsewhere); on every other platform the same function is a stub that
 * reports the missing backend. No driver outside Apple platforms consumes MSL,
 * so the stub is unreachable — keeping the declaration unconditional just means
 * the callers need no `#if`.
 */
#ifndef XT_GUI_WEBGL_MSL_H
#define XT_GUI_WEBGL_MSL_H

#include <cstdint>
#include <string>
#include <vector>

namespace xtgui {

/**
 * Cross-compile one SPIR-V stage to MSL.
 *
 * `source` receives the MSL text, `entry` the entry-point name (Metal reserves
 * `main`, so SPIRV-Cross renames it). On failure `log` explains why and the
 * other outputs are left empty.
 */
bool cross_compile_msl(const std::vector<uint32_t> &spirv, bool fragment,
                       std::string *source, std::string *entry, std::string *log);

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_MSL_H */
