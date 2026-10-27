/*
 * xbintsc GUI engine — shared internals of the Web Audio surface.
 *
 * `audio.cpp` owns the context/node objects, while `audio_param_object.cpp`
 * owns the AudioParam object and its scheduled methods. Both need to turn a JS
 * object back into `(mixer, node)` and to build a parameter, so those three
 * pieces live behind this small header instead of being duplicated.
 */
#ifndef XT_GUI_AUDIO_INTERNAL_H
#define XT_GUI_AUDIO_INTERNAL_H

#include <cstdint>
#include <memory>

#include "audio_mixer.h"
#include "rt.h"

namespace xtgui {

/** The live mixer for a context id, or null after `close()`. */
std::shared_ptr<AudioMixer> audio_mixer_by_id(uint64_t id);
/** The context id stored on a context/node/param object (0 if absent). */
uint64_t audio_context_id(xt_value object);

/** Build the `AudioParam` value that belongs to `node`'s slot `index`. */
xt_value audio_param_build(uint64_t context, uint64_t node, int index);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_INTERNAL_H */
