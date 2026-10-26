/*
 * xbintsc GUI engine — WebGL 1.0 context on SDL_GPU.
 *
 * A `WebGLContext` owns one offscreen RGBA8 colour texture (plus a depth
 * texture) and records GPU work into its own SDL_GPU command buffer. The
 * texture the page composites is that colour target, so a draw reaches the
 * screen without a CPU round-trip: geometry, lighting, texture sampling and
 * blending all execute on the GPU.
 *
 * WebGL's origin is bottom-left while SDL_GPU render targets use a top-left
 * origin (its Vulkan backend flips the viewport), so the framebuffer is stored
 * top-down; `viewport`/`scissor` are converted and `readPixels` flips rows back
 * to the GL convention.
 *
 * Shaders are translated to SPIR-V by `webgl_glsl.cpp` (glslang). Pipeline
 * objects depend on the vertex layout and fixed-function state, so they are
 * created lazily per (program, layout, state) combination and cached.
 *
 * Handles are integer ids, not pointers: a `deleteBuffer`/`deleteTexture` call
 * must make an old JavaScript handle a no-op rather than a dangling pointer.
 */
#ifndef XT_GUI_WEBGL_H
#define XT_GUI_WEBGL_H

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "webgl_constants.h"
#include "webgl2_constants.h"
#include "webgl_glsl.h"

namespace xtgui {

constexpr int kWebGLMaxAttribs = 16;
constexpr int kWebGLMaxTextureUnits = 16;
constexpr int kWebGLMaxDrawBuffers = 4;

struct WebGLShader {
  uint32_t id = 0;
  int type = 0;  // GL_VERTEX_SHADER / GL_FRAGMENT_SHADER
  std::string source;
  std::string log;
  bool compiled = false;
  bool delete_requested = false;
  int attachments = 0;
};

/** One active uniform of a linked program, with its std140 home. */
struct WebGLUniform {
  std::string name;
  GlslBlockMember layout;
  bool sampler = false;
  int unit = -1;
};

struct WebGLProgram {
  uint32_t id = 0;
  uint32_t vs = 0;
  uint32_t fs = 0;
  bool linked = false;
  std::string log;
  std::vector<WebGLUniform> uniforms;
  std::vector<GlslUniformDecl> attribs;
  std::unordered_map<std::string, int> attrib_locations;  // name -> location
  std::unordered_map<std::string, int> attrib_bindings;   // bindAttribLocation overrides
  std::unordered_map<std::string, int> uniform_index;     // name -> uniforms[] index
  std::vector<int> vs_sampler_uniforms;  // local binding -> uniforms[] index
  std::vector<int> fs_sampler_uniforms;
  std::vector<uint8_t> block;  // std140 uniform data
  int block_size = 0;
  bool block_dirty = true;
  bool vs_has_block = false;
  bool fs_has_block = false;
  SDL_GPUShader *vs_shader = nullptr;
  SDL_GPUShader *fs_shader = nullptr;
  std::vector<SDL_GPUGraphicsPipeline *> pipelines;
  std::unordered_map<std::string, SDL_GPUGraphicsPipeline *> pipeline_cache;
};

struct WebGLBuffer {
  uint32_t id = 0;
  std::vector<uint8_t> data;
  SDL_GPUBuffer *gpu = nullptr;
  uint32_t capacity = 0;
  bool dirty = true;
  /** The `usage` hint from `bufferData` (`STATIC_DRAW`, `DYNAMIC_DRAW`, …).
   * It is cache behaviour rather than rendering state, but `getBufferParameter`
   * reports it and dynamic buffers may be rewritten every frame. */
  int usage = GL_STATIC_DRAW;
};

struct WebGLTexture {
  uint32_t id = 0;
  int width = 0;
  int height = 0;
  int levels = 1;
  /** The binding target this texture was created for. */
  int target = GL_TEXTURE_2D;
  /** Layers for a cube/2D-array texture, depth for a 3D texture. */
  int layers = 1;
  SDL_GPUTexture *gpu = nullptr;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  /** Depth(-stencil) formats can only be depth attachments. */
  bool depth = false;
  /** Block-compressed textures are sample-only, with one mip level. */
  bool compressed = false;
  /** Usage flags the current `gpu` texture was created with. */
  uint32_t usage = 0;
  bool as_target = false;        // bound as a colour attachment
  bool as_depth_target = false;  // bound as a depth attachment
  bool has_mipmaps = false;
  int min_filter = 0;
  int mag_filter = 0;
  int wrap_s = 0;
  int wrap_t = 0;
  SDL_GPUSampler *sampler = nullptr;
  bool dirty = false;
  bool defined = false;
};

struct WebGLRenderbuffer {
  uint32_t id = 0;
  int width = 0;
  int height = 0;
  bool stencil = false;
  SDL_GPUTexture *depth = nullptr;
};

struct WebGLFramebuffer {
  uint32_t id = 0;
  uint32_t color_textures[kWebGLMaxDrawBuffers] = {};
  /** Layer rendered to for an array/cube colour attachment. */
  int color_layers[kWebGLMaxDrawBuffers] = {};
  /** `COLOR_ATTACHMENTi` or `NONE` per draw-buffer slot. */
  int draw_buffers[kWebGLMaxDrawBuffers] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE};
  uint32_t depth_texture = 0;
  uint32_t depth_renderbuffer = 0;
  int width = 0;
  int height = 0;
  SDL_GPUTexture *depth = nullptr;  // owned when no renderbuffer/texture supplies one
};

/** Per-attribute array state. */
struct WebGLAttribState {
  bool enabled = false;
  bool integer = false;  // `vertexAttribIPointer` rather than `vertexAttribPointer`
  uint32_t buffer = 0;
  int size = 4;
  int type = GL_FLOAT;
  bool normalized = false;
  int stride = 0;
  int offset = 0;
  int divisor = 0;  // 0 = per vertex, 1 = per instance
  float current[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

/** A WebGL 2 vertex-array object; binding one restores this state. */
struct WebGLVertexArray {
  uint32_t id = 0;
  WebGLAttribState attribs[kWebGLMaxAttribs];
  uint32_t element_buffer = 0;
};

/** A WebGL 2 sampler object, bound to a texture unit with `bindSampler`. */
struct WebGLSampler {
  uint32_t id = 0;
  int min_filter = 0;
  int mag_filter = 0;
  int wrap_s = 0;
  int wrap_t = 0;
  SDL_GPUSampler *gpu = nullptr;
};

struct WebGLQuery {
  uint32_t id = 0;
};

struct WebGLSync {
  uint32_t id = 0;
};

/** One face's stencil test and update state. */
struct WebGLStencilFace {
  int func = GL_ALWAYS;
  int ref = 0;
  uint32_t value_mask = 0xFFFFFFFFu;  // compare mask
  uint32_t write_mask = 0xFFFFFFFFu;
  int fail = GL_KEEP;
  int zfail = GL_KEEP;
  int zpass = GL_KEEP;
};

/** The fixed-function state a pipeline depends on. */
struct WebGLDrawState {
  bool blend = false;
  int blend_src_rgb = GL_ONE;
  int blend_dst_rgb = GL_ZERO;
  int blend_src_alpha = GL_ONE;
  int blend_dst_alpha = GL_ZERO;
  int blend_eq_rgb = GL_FUNC_ADD;
  int blend_eq_alpha = GL_FUNC_ADD;
  bool depth_test = false;
  bool depth_write = true;
  int depth_func = GL_LESS;
  bool stencil = false;
  WebGLStencilFace stencil_front;
  WebGLStencilFace stencil_back;
  bool cull = false;
  int cull_face = GL_BACK;
  int front_face = GL_CCW;
  bool color_mask[4] = {true, true, true, true};
  /** Attachment formats of the bound target; pipelines are format-specific. */
  int color_formats[kWebGLMaxDrawBuffers] = {
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
  int color_target_count = 1;
  int depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
};

class WebGLContext {
 public:
  WebGLContext(uint64_t id, int width, int height, bool webgl2);
  ~WebGLContext();

  uint64_t id() const { return id_; }
  bool is_webgl2() const { return webgl2_; }
  int width() const { return width_; }
  int height() const { return height_; }
  /** Resize the drawing buffer; GL semantics discard its contents. */
  void resize(int width, int height);

  /** End the current pass and submit the queued commands (called once per
   * frame, before the page's own render pass samples the colour texture). */
  void submit();
  SDL_GPUTexture *color_texture();
  bool lost() const { return lost_; }
  /** Simulate a loss of the GPU device (`WEBGL_lose_context.loseContext`).
   * Every object is destroyed, as the WebGL spec requires; `restore_context`
   * brings the context back with an empty object table. */
  void lose_context();
  void restore_context();
  /** WebGL 2 `drawingBufferStorage`: fix the drawing-buffer size so a later
   * canvas resize no longer changes it. */
  void drawing_buffer_storage(int width, int height);

  /* -- object lookups ---------------------------------------------------- */
  WebGLShader *shader(uint32_t id);
  WebGLProgram *program(uint32_t id);
  WebGLBuffer *buffer(uint32_t id);
  WebGLTexture *texture(uint32_t id);
  WebGLFramebuffer *framebuffer(uint32_t id);
  WebGLRenderbuffer *renderbuffer(uint32_t id);
  WebGLVertexArray *vertex_array(uint32_t id);
  WebGLSampler *sampler(uint32_t id);
  WebGLQuery *query(uint32_t id);
  WebGLSync *sync(uint32_t id);

  /* -- state ------------------------------------------------------------- */
  void enable(int cap);
  void disable(int cap);
  bool is_enabled(int cap) const;
  void clear_color(float r, float g, float b, float a);
  void clear_depth(float depth) { clear_depth_ = depth; }
  void clear_stencil(int value) { clear_stencil_ = value; }
  void color_mask(bool r, bool g, bool b, bool a);
  void depth_mask(bool write) { state_.depth_write = write; }
  void depth_func(int func) { state_.depth_func = func; }
  void stencil_func(int func, int ref, uint32_t mask);
  void stencil_func_separate(int face, int func, int ref, uint32_t mask);
  void stencil_op(int fail, int zfail, int zpass);
  void stencil_op_separate(int face, int fail, int zfail, int zpass);
  void stencil_mask(uint32_t mask);
  void stencil_mask_separate(int face, uint32_t mask);
  const WebGLStencilFace &stencil_face(int face) const;
  int stencil_reference() const;  // the value the GPU test sees
  void blend_func(int src, int dst);
  void blend_func_separate(int src_rgb, int dst_rgb, int src_alpha, int dst_alpha);
  void blend_equation(int mode);
  void blend_equation_separate(int rgb, int alpha);
  void blend_color(float r, float g, float b, float a);
  void blend_color_value(float out[4]) const;
  void cull_face(int mode);
  void front_face(int mode);
  void viewport(int x, int y, int w, int h);
  void scissor(int x, int y, int w, int h);
  void clear(int mask);
  void finish();

  /* -- shaders and programs --------------------------------------------- */
  uint32_t create_shader(int type);
  void delete_shader(uint32_t id);
  void shader_source(uint32_t id, const std::string &source);
  bool compile_shader(uint32_t id);
  std::string shader_info_log(uint32_t id);
  bool shader_compiled(uint32_t id);
  std::string shader_source_text(uint32_t id);

  uint32_t create_program();
  void delete_program(uint32_t id);
  bool attach_shader(uint32_t program, uint32_t shader);
  bool detach_shader(uint32_t program, uint32_t shader);
  bool link_program(uint32_t id);
  void use_program(uint32_t id);
  bool program_linked(uint32_t id);
  std::string program_info_log(uint32_t id);
  std::vector<uint32_t> attached_shaders(uint32_t id);
  int active_uniform_count(uint32_t id);
  int active_attrib_count(uint32_t id);
  const WebGLUniform *active_uniform(uint32_t id, int index);
  const GlslUniformDecl *active_attrib(uint32_t id, int index);

  int uniform_location(uint32_t program, const std::string &name);
  int attrib_location(uint32_t program, const std::string &name);
  void bind_attrib_location(uint32_t program, int index, const std::string &name);

  /* Uniform uploads: `location` is an index into the program's uniform table
   * (the DOM layer wraps it in the `WebGLUniformLocation` handle). */
  void uniform_floats(uint32_t program, int location, int components, const double *values,
                      int count);
  void uniform_ints(uint32_t program, int location, int components, const int *values, int count);
  void uniform_matrix(uint32_t program, int location, int dimensions, const double *values);
  bool uniform_values(uint32_t program, int location, std::vector<double> *out);

  /* -- buffers and vertex attributes ------------------------------------ */
  uint32_t create_buffer();
  void delete_buffer(uint32_t id);
  void bind_buffer(int target, uint32_t id);
  bool buffer_data(uint32_t id, const uint8_t *bytes, uint32_t size, int usage);
  bool buffer_sub_data(uint32_t id, uint32_t offset, const uint8_t *bytes, uint32_t size);
  uint32_t buffer_size(uint32_t id);
  void enable_attrib(int index, bool enabled);
  bool attrib_enabled(int index) const;
  void attrib_pointer(int index, int size, int type, bool normalized, int stride, int offset);
  void attrib_value(int index, int components, const float *values);
  void attrib_state(int index, int *size, int *type, int *stride, bool *normalized,
                    float current[4]) const;
  uint32_t attrib_buffer(int index) const { return attribs_[index].buffer; }
  void attrib_ipointer(int index, int size, int type, int stride, int offset);
  void attrib_divisor(int index, int divisor);

  /* -- WebGL 2 vertex arrays, instancing and buffer copies --------------- */
  uint32_t create_vertex_array();
  void delete_vertex_array(uint32_t id);
  void bind_vertex_array(uint32_t id);
  bool is_vertex_array(uint32_t id) const;
  uint32_t bound_vertex_array() const { return bound_vertex_array_; }
  void draw_arrays_instanced(int mode, int first, int count, int instances);
  void draw_elements_instanced(int mode, int count, int type, int offset, int instances);
  void draw_range_elements(int mode, int start, int end, int count, int type, int offset);
  bool copy_buffer_sub_data(int read_target, int write_target, uint32_t read_offset,
                            uint32_t write_offset, uint32_t size);
  bool get_buffer_sub_data(int target, uint32_t offset, uint8_t *out, uint32_t size);

  /* -- WebGL 2 sampler objects ------------------------------------------- */
  uint32_t create_sampler();
  void delete_sampler(uint32_t id);
  void bind_sampler(int unit, uint32_t id);
  bool sampler_parameter(uint32_t id, int pname, int value);
  int sampler_parameter_value(uint32_t id, int pname);

  /* -- WebGL 2 queries, syncs and uniform-block stubs -------------------- */
  uint32_t create_query();
  void delete_query(uint32_t id);
  bool is_query(uint32_t id) const;
  uint32_t create_sync();
  void delete_sync(uint32_t id);
  bool is_sync(uint32_t id) const;
  uint32_t uniform_block_index(uint32_t program, const std::string &name) const;

  /* -- textures ---------------------------------------------------------- */
  uint32_t create_texture();
  void delete_texture(uint32_t id);
  void bind_texture(int target, uint32_t id);
  void active_texture(int unit);
  bool tex_parameter(uint32_t texture, int pname, int value);
  int tex_parameter_value(uint32_t id, int pname);
  bool tex_image_rgba(uint32_t id, int width, int height, const uint8_t *rgba);
  bool tex_sub_image_rgba(uint32_t id, int x, int y, int width, int height, const uint8_t *rgba);
  /** Generic level-0 upload: `pixels` is tightly packed in `format` (always
   * four components; R/RG formats are expanded by the caller). */
  bool tex_image_format(uint32_t id, int width, int height, SDL_GPUTextureFormat format,
                        const uint8_t *pixels, int layer = -1);
  bool tex_sub_image_format(uint32_t id, int x, int y, int width, int height,
                            const uint8_t *pixels, int layer = 0);
  bool generate_mipmap(uint32_t id);
  /** WebGL 2 `texStorage2D`: allocate immutable storage at `width`×`height`.
   * The backend always allocates the full mip chain, so `levels` is a hint. */
  bool tex_storage_2d(uint32_t id, int levels, int width, int height);
  bool tex_storage_format(uint32_t id, int levels, int width, int height,
                          SDL_GPUTextureFormat format);
  /** Allocate (and optionally upload) a 3D or 2D-array texture. */
  bool tex_storage_3d(uint32_t id, int levels, int width, int height, int depth,
                      SDL_GPUTextureFormat format, int target);
  bool tex_image_3d(uint32_t id, int width, int height, int depth,
                    SDL_GPUTextureFormat format, const uint8_t *pixels, int target);
  bool tex_sub_image_3d(uint32_t id, int x, int y, int z, int width, int height, int depth,
                        const uint8_t *pixels);
  /** Upload a block-compressed region (`data` holds the tightly packed blocks
   * for the `width`×`height` region). */
  bool tex_image_compressed(uint32_t id, int width, int height, SDL_GPUTextureFormat format,
                            int x, int y, int region_width, int region_height,
                            const uint8_t *data, uint32_t size);
  /** Whether the device can sample `internalformat` (compressed formats). */
  bool supports_texture_format(int internalformat);
  int texture_width(uint32_t id);
  int texture_height(uint32_t id);
  /** The SDL_GPU format of a texture (RGBA8 before `texImage2D`). */
  int texture_format(uint32_t id);

  /* -- framebuffers ------------------------------------------------------ */
  uint32_t create_framebuffer();
  void delete_framebuffer(uint32_t id);
  void bind_framebuffer(int target, uint32_t id);
  uint32_t bound_read_framebuffer_id() const { return read_framebuffer_; }
  bool blit_framebuffer(const int source[4], const int destination[4], int mask, int filter);
  bool framebuffer_texture(uint32_t framebuffer, uint32_t texture,
                           int attachment = GL_COLOR_ATTACHMENT0);
  bool framebuffer_texture_layer(uint32_t framebuffer, uint32_t texture, int attachment,
                                 int layer);
  bool framebuffer_status(uint32_t id);
  /** WebGL 2 `drawBuffers` / `readBuffer`. */
  void draw_buffers(int count, const int *buffers);
  void read_buffer(int mode) { read_buffer_ = mode; }
  int read_buffer_value() const { return read_buffer_; }
  int draw_buffer_value(int index);
  uint32_t create_renderbuffer();
  void delete_renderbuffer(uint32_t id);
  void bind_renderbuffer(uint32_t id) { bound_renderbuffer_ = id; }
  bool renderbuffer_storage(uint32_t id, int width, int height, bool stencil);
  bool framebuffer_renderbuffer(uint32_t framebuffer, uint32_t renderbuffer);

  /* -- drawing ----------------------------------------------------------- */
  void draw_arrays(int mode, int first, int count);
  void draw_elements(int mode, int count, int type, int offset);
  bool read_pixels(int x, int y, int width, int height, uint8_t *out);
  /** WebGL 2 `clearBufferfv` / `clearBufferiv` / `clearBufferuiv`. */
  void clear_buffer_float(int buffer, int drawbuffer, const float *values, int count);
  void clear_buffer_int(int buffer, int drawbuffer, const int *values, int count);
  void clear_buffer_uint(int buffer, int drawbuffer, const unsigned int *values, int count);
  void clear_buffer_fi(int buffer, int drawbuffer, float depth, int stencil);

  /* -- queries ----------------------------------------------------------- */
  double parameter(int pname);
  std::string string_parameter(int pname);
  uint32_t bound_buffer(int target) const;
  uint32_t bound_texture(int unit) const;
  uint32_t bound_cube_texture(int unit) const;
  uint32_t bound_array_texture(int unit) const;
  uint32_t bound_3d_texture(int unit) const;
  uint32_t current_program_id() const { return current_program_; }
  uint32_t bound_framebuffer_id() const { return bound_framebuffer_; }
  uint32_t bound_renderbuffer_id() const { return bound_renderbuffer_; }
  void viewport_box(int out[4]) const;
  void scissor_box(int out[4]) const;
  int error() {
    int value = error_;
    error_ = 0;
    return value;
  }
  void set_error(int value) {
    if (error_ == 0) error_ = value;
  }
  const char *driver() const;

 private:
  /* -- GPU plumbing ------------------------------------------------------ */
  void ensure_command();
  void end_pass();
  void flush_uploads();
  void begin_pass(bool clear_color, bool clear_depth, bool clear_stencil);
  /** Scissored depth/stencil clear via a masked fullscreen triangle. */
  void scissored_clear(int mask);
  /** The 1×1 texture a scissored colour clear blits from. */
  SDL_GPUTexture *clear_color_texture();
  SDL_GPUTexture *framebuffer_color(uint32_t id);
  void framebuffer_size(uint32_t id, int *width, int *height);
  /** Release every GPU handle this context owns, keeping the CPU-side object
   * table. Used by the destructor and by `lose_context`. */
  void release_gpu_resources();
  SDL_GPUTexture *target_color();
  SDL_GPUTexture *target_depth();
  int target_color_format();
  int target_depth_format();
  /** The texture rendered to for draw-buffer slot `slot`, or null. */
  SDL_GPUTexture *draw_color_texture(int slot);
  int draw_color_format(int slot);
  int draw_color_layer(int slot);
  SDL_GPUTexture *ensure_texture_gpu(WebGLTexture *texture, bool as_target);
  SDL_GPUSampler *sampler_for(WebGLTexture *texture);
  SDL_GPUBuffer *ensure_buffer_gpu(WebGLBuffer *buffer);
  void ensure_default_target();
  void rebuild_default_target();
  bool ensure_fallback();
  SDL_GPUBuffer *upload_temp_buffer(const void *data, uint32_t size,
                                    SDL_GPUBufferUsageFlags usage);
  void draw(int mode, int first, int count, int index_type, int offset, int instances);
  void save_vertex_array();
  void load_vertex_array(const WebGLVertexArray *array);
  SDL_GPUSampler *sampler_gpu(WebGLSampler *sampler);
  uint32_t buffer_for_target(int target) const;
  void release_program_pipelines(WebGLProgram *program);
  SDL_GPUGraphicsPipeline *create_pipeline(WebGLProgram *program, int mode, int index_type,
                                           int attribute_count, int buffer_count,
                                           const SDL_GPUVertexAttribute *attrs,
                                           const SDL_GPUVertexBufferDescription *buffers,
                                           const std::string &key);

  uint64_t id_ = 0;
  int width_ = 0;
  int height_ = 0;
  bool lost_ = false;
  bool webgl2_ = false;

  SDL_GPUCommandBuffer *cmd_ = nullptr;
  SDL_GPURenderPass *pass_ = nullptr;
  bool pass_targets_default_ = true;
  uint32_t pass_framebuffer_ = 0;

  SDL_GPUTexture *color_ = nullptr;
  SDL_GPUTexture *depth_ = nullptr;
  SDL_GPUTexture *fallback_texture_ = nullptr;
  SDL_GPUSampler *fallback_sampler_ = nullptr;
  /** 1×1 RGBA8 source for a scissored colour clear. */
  SDL_GPUTexture *clear_texture_ = nullptr;
  /** Internal program/buffer for a scissored depth/stencil clear. */
  uint32_t clear_program_ = 0;
  uint32_t clear_buffer_ = 0;

  float clear_color_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float clear_depth_ = 1.0f;
  int clear_stencil_ = 0;
  float blend_color_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  /** Set by `drawingBufferStorage`; canvas resizes then leave the size. */
  bool fixed_storage_ = false;
  WebGLDrawState state_;
  int viewport_[4] = {0, 0, 0, 0};
  int scissor_box_[4] = {0, 0, 0, 0};
  bool scissor_test_ = false;

  std::unordered_map<uint32_t, std::unique_ptr<WebGLShader>> shaders_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLProgram>> programs_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLBuffer>> buffers_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLTexture>> textures_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLFramebuffer>> framebuffers_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLRenderbuffer>> renderbuffers_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLVertexArray>> vertex_arrays_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLSampler>> samplers_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLQuery>> queries_;
  std::unordered_map<uint32_t, std::unique_ptr<WebGLSync>> syncs_;
  /** GPU buffers for transient draws (constant attributes, expanded indices). */
  std::vector<SDL_GPUBuffer *> temp_buffers_;

  uint32_t current_program_ = 0;
  uint32_t bound_array_ = 0;
  uint32_t bound_element_ = 0;
  uint32_t bound_framebuffer_ = 0;
  uint32_t read_framebuffer_ = 0;
  int read_buffer_ = GL_BACK;
  int default_draw_buffers_[kWebGLMaxDrawBuffers] = {GL_BACK, GL_NONE, GL_NONE, GL_NONE};
  uint32_t bound_renderbuffer_ = 0;
  uint32_t bound_textures_[kWebGLMaxTextureUnits] = {};
  uint32_t bound_cube_textures_[kWebGLMaxTextureUnits] = {};
  uint32_t bound_array_textures_[kWebGLMaxTextureUnits] = {};
  uint32_t bound_3d_textures_[kWebGLMaxTextureUnits] = {};
  /** The target of the most recent `bindTexture` on each unit, so the existing
   * per-unit entry points operate on the object the page just bound. */
  int active_texture_target_[kWebGLMaxTextureUnits] = {};
  uint32_t bound_samplers_[kWebGLMaxTextureUnits] = {};
  uint32_t bound_vertex_array_ = 0;
  WebGLVertexArray default_vertex_array_;
  uint32_t copy_read_buffer_ = 0;
  uint32_t copy_write_buffer_ = 0;
  int active_unit_ = 0;
  WebGLAttribState attribs_[kWebGLMaxAttribs];

  uint32_t next_object_id_ = 1;
  int error_ = 0;
};

/** Resolve a process-wide WebGL context id (used by the renderer when it paints
 * a `<canvas>`). */
WebGLContext *xt_webgl_by_id(uint64_t id);

/** Map a WebGL internal format to the SDL_GPU texture format the engine uses
 * (RGBA8 for formats without a special backend representation). */
SDL_GPUTextureFormat webgl_storage_format(int internalformat);

/** Map a WebGL compressed-texture internal format to its SDL_GPU format, or
 * `SDL_GPU_TEXTUREFORMAT_INVALID` for anything else. */
SDL_GPUTextureFormat webgl_compressed_format(int internalformat);

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_H */
