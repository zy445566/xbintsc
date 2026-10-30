#ifndef XT_GUI_MEDIA_API_H
#define XT_GUI_MEDIA_API_H

/*
 * The C ABI between the GUI engine and an optional media decoder backend.
 *
 * The engine knows how to model `<video>`/`<audio>` elements and how to draw
 * their frames, but it deliberately contains no decoder: FFmpeg lives in the
 * optional `media` link feature (`runtime/ext_media/`, linked as
 * `media.a`, or `media.lib` with the MSVC ABI on Windows). Keeping the boundary C-only means the backend can be a prebuilt
 * C++ archive that is compiled independently of the engine.
 *
 * Registration is push-based: the backend's bridge translation unit is linked
 * only when the compiler selects the feature (it is the feature's
 * `runtimeSources()` entry), and a
 * constructor calls `xt_media_install` with its function table. The GUI's
 * dispatch layer (`media.cpp`) starts with a null table, so a program that does
 * not use media still links and simply reports that media support was
 * not linked.
 *
 * `audio_sink` is an opaque `xtgui::AudioSink*` owned by the caller until the
 * open call; the backend takes ownership whether the open succeeds or not.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque decoder handle (`xtgui::MediaPlayer*`). */
typedef void *XtMediaPlayerHandle;

/** Open `path`, returning null on failure and copying a message into `error`
 * (which is null-terminated and may be an empty string). */
typedef XtMediaPlayerHandle (*XtMediaOpenFn)(const char *path, char *error,
                                             size_t error_size, void *audio_sink);
/** The live decoder for a process-wide id, or null. */
typedef XtMediaPlayerHandle (*XtMediaByIdFn)(uint64_t id);
/** Tear down every decoder and process-wide media state. */
typedef void (*XtMediaShutdownFn)(void);

/** Interleaved float PCM returned by the memory decoder.
 *
 * `samples` is allocated by the backend and must be released with the table's
 * `free_audio` entry point (both archives use the same static CRT). */
typedef struct XtMediaAudioBuffer {
  int channels;
  int sample_rate;
  int frames;
  float *samples;
} XtMediaAudioBuffer;

/** Decode an in-memory audio file (`decodeAudioData`). Returns 1 on success and
 * fills `out`; on failure returns 0 and copies a message into `error`. */
typedef int (*XtMediaDecodeAudioFn)(const unsigned char *data, size_t size,
                                    XtMediaAudioBuffer *out, char *error, size_t error_size);
/** Release the `samples` a successful `decode_audio` allocated. */
typedef void (*XtMediaFreeAudioFn)(void *samples);

typedef struct XtMediaApi {
  XtMediaOpenFn open;
  XtMediaByIdFn by_id;
  XtMediaShutdownFn shutdown;
  XtMediaDecodeAudioFn decode_audio;
  XtMediaFreeAudioFn free_audio;
} XtMediaApi;

/** Install a decoder table. Implemented by the GUI engine; called once by the
 * media backend's bridge object before `main`. */
void xt_media_install(const XtMediaApi *api);

/** Decoder backend entry points, implemented by `runtime/ext_media/media.cpp`
 * (inside `media.a`/`media.lib`). The bridge references them, which is what makes the
 * linker extract the archive member when the `media` feature is selected. */
XtMediaPlayerHandle xt_media_backend_open(const char *path, char *error,
                                          size_t error_size, void *audio_sink);
XtMediaPlayerHandle xt_media_backend_by_id(uint64_t id);
void xt_media_backend_shutdown(void);
int xt_media_backend_decode_audio(const unsigned char *data, size_t size,
                                  XtMediaAudioBuffer *out, char *error, size_t error_size);
void xt_media_backend_free_audio(void *samples);

#ifdef __cplusplus
}
#endif

#endif /* XT_GUI_MEDIA_API_H */
