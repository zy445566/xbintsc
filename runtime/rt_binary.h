/*
 * xbintsc runtime — binary data: `ArrayBuffer`, typed arrays and `DataView`.
 *
 * Included by `rt.h`. An `ArrayBuffer` owns one `calloc`ed byte block (see
 * `xt_object.buffer_bytes`), and a typed-array view caches the block plus its
 * byte offset, so element access is a `memcpy`-sized read/write instead of a
 * property lookup. The helpers here are what the compiler, the standard library
 * and extensions (audio/image decoders, wasm memories) share.
 */

#ifndef XT_RT_BINARY_H
#define XT_RT_BINARY_H

/** Typed-array constructor/static/prototype dispatch by constructor name. */
xt_value xt_typed_array_ctor_by_name(xt_value name, int32_t argc, xt_value *argv);
xt_value xt_typed_array_static_by_name(xt_value name, xt_value method, int32_t argc, xt_value *argv);
xt_value xt_typed_array_prototype(xt_value name);

/** Bulk access to the bytes an `ArrayBuffer` (or a typed array's backing
 * store) holds, for decoders and encoders that cannot afford one property read
 * per byte. `offset` and `count` are clamped to the buffer and the number of
 * bytes actually transferred is returned. Extensions pass the backing buffer
 * plus a view's `byteOffset`/`byteLength`; see `runtime/ext_gui/audio.cpp`. */
uint32_t xt_array_buffer_copy_bytes_out(xt_value buffer, uint32_t offset, uint8_t *dst,
                                        uint32_t count);
uint32_t xt_array_buffer_copy_bytes_in(xt_value buffer, uint32_t offset, const uint8_t *src,
                                       uint32_t count);
/** Allocate a zero-filled `ArrayBuffer` of `byte_length` bytes (the native
 * counterpart of `new ArrayBuffer(n)`). */
xt_value xt_array_buffer_alloc(uint32_t byte_length);

/** Typed-array view internals for extensions: whether `value` is a view, the
 * `ArrayBuffer` it views, and the byte offset of the view inside it. */
int xt_is_typed_array(xt_value value);
/** Element kind (0..8: U8, I8, U8C, U16, I16, U32, I32, F32, F64), or -1. */
int xt_typed_array_kind_of(xt_value value);
/** Read one element (coerced to a number) from a typed-array view. */
xt_value xt_typed_array_read(xt_value array, uint32_t index);
xt_value xt_typed_array_backing_buffer(xt_value value);
uint32_t xt_typed_array_view_offset(xt_value value);
/** Number of elements in a typed-array view. */
uint32_t xt_typed_array_length(xt_value value);

#endif /* XT_RT_BINARY_H */
