/*
 * xbintsc GUI engine — WebGPU pass resource binding.
 *
 * SDL_GPU binds by flat slot (sampler N, storage buffer N, uniform slot N), so
 * each draw/dispatch walks the pipeline's translated stage resource lists and
 * resolves the matching WebGPU bind-group entries.
 */
#ifndef XT_GUI_WEBGPU_PASS_H
#define XT_GUI_WEBGPU_PASS_H

#include "webgpu.h"

namespace xtgui {
namespace webgpu {

std::shared_ptr<BindGroup> bound_group(const std::map<int, BindGroupSlot> &groups, int index);
const BindGroupEntry *bound_entry(const std::map<int, BindGroupSlot> &groups, int group,
                                  int binding, int *dynamic_offset);
SDL_GPUTexture *view_texture(uint32_t view_id);
SDL_GPUTexture *fallback_texture(Device &device);
SDL_GPUSampler *fallback_sampler(Device &device);

/** Bind every resource the vertex and fragment stages use. */
bool bind_render_resources(RenderPassEncoder &pass, RenderPipeline &pipeline,
                           SDL_GPUCommandBuffer *command);
/** Bind a compute stage's read resources; begins the SDL pass if needed. */
bool bind_compute_resources(ComputePassEncoder &pass, ComputePipeline &pipeline,
                            SDL_GPUCommandBuffer *command);

}  // namespace webgpu
}  // namespace xtgui

#endif /* XT_GUI_WEBGPU_PASS_H */
