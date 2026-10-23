#ifndef XT_GUI_MEDIA_API_H
#define XT_GUI_MEDIA_API_H

/*
 * The C ABI between the GUI engine and an optional media decoder backend.
 *
 * The engine knows how to model `<video>`/`<audio>` elements and how to draw
 * their frames, but it deliberately contains no decoder: FFmpeg lives in the
 * separate `media` compile extension (`runtime/ext_media/`, linked as
 * `media.a`). Keeping the boundary C-only means the backend can be a prebuilt
 * C++ archive that is compiled independently of the engine.
 *
 * Registration is push-based: the backend's bridge translation unit is linked
 * directly (it is a `runtimeSources()` entry of the media extension), and a
 * constructor calls `xt_media_install` with its function table. The GUI's
 * dispatch layer (`media.cpp`) starts with a null table, so a program built
 * with `--ext gui` alone still links and simply reports that media support was
 * not enabled.
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

typedef struct XtMediaApi {
  XtMediaOpenFn open;
  XtMediaByIdFn by_id;
  XtMediaShutdownFn shutdown;
} XtMediaApi;

/** Install a decoder table. Implemented by the GUI engine; called once by the
 * media backend's bridge object before `main`. */
void xt_media_install(const XtMediaApi *api);

/** Decoder backend entry points, implemented by `runtime/ext_media/media.cpp`
 * (inside `media.a`). The bridge references them, which is what makes the
 * linker extract the archive member when the media extension is enabled. */
XtMediaPlayerHandle xt_media_backend_open(const char *path, char *error,
                                          size_t error_size, void *audio_sink);
XtMediaPlayerHandle xt_media_backend_by_id(uint64_t id);
void xt_media_backend_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* XT_GUI_MEDIA_API_H */
