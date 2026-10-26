/*
 * xbintsc GUI engine — uniform-block bookkeeping shared by programs and draws.
 */
#ifndef XT_GUI_WEBGL_UNIFORM_BLOCK_H
#define XT_GUI_WEBGL_UNIFORM_BLOCK_H

#include <cstdint>
#include <string>

namespace xtgui {

/** A user uniform block merged into the engine's single std140 block. */
struct WebGLUniformBlock {
  std::string name;
  std::string instance;
  int binding = 0;
  int base_offset = 0;
  int data_size = 0;
  int member_count = 0;
};

/** The buffer bound to one indexed uniform-buffer binding point. */
struct WebGLUniformBinding {
  uint32_t buffer = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
};

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_UNIFORM_BLOCK_H */
