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

/** The `AudioContext.listener` object: nine AudioParams the panners read. */
xt_value audio_listener_object(xt_value context_value);

/* -- helpers shared with `audio_nodes.cpp` --------------------------------- */

/** Wrap a native `(self, env, argc, argv)` function as a closure value. */
xt_value audio_fn_value(void *fn);
void audio_set_method(xt_value target, const char *name, void *fn);
void audio_set_number(xt_value target, const char *name, double value);
void audio_set_string(xt_value target, const char *name, const char *value);
double audio_number_prop(xt_value object, const char *name);

/** The mixer behind a context/node/param object plus the node's native id. */
std::shared_ptr<AudioMixer> audio_mixer_of(xt_value object);
bool audio_node_ref(xt_value object, uint64_t *context, uint64_t *node);

/** Create a node object with the uniform methods, `channelCount` and one
 * AudioParam per entry of `params`. Used by every `create*` factory. */
xt_value audio_build_node(uint64_t context, AudioNodeKind kind, const char *type,
                          const char *const *params, int param_count);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_INTERNAL_H */
