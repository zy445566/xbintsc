/*
 * xbintsc GUI engine — per-quantum effect nodes split out of `audio_mixer.cpp`
 * so the mixer file stays focused on graph traversal and sources.
 *
 * Both functions read the node's AudioParams at the quantum start (the Web
 * Audio model keeps a biquad's coefficients and a compressor's envelope stable
 * for one 128-frame quantum) and write the processed stereo quantum back into
 * `node->quantum`.
 */
#ifndef XT_GUI_AUDIO_EFFECTS_H
#define XT_GUI_AUDIO_EFFECTS_H

#include "audio_mixer.h"

namespace xtgui {

/** Biquad filter: one coefficient set per quantum, per-channel state. */
void render_biquad(AudioNode *node, const float *input, double quantum_start);

/** Dynamics compressor: stereo-linked peak detector with attack/release. */
void render_compressor(AudioNode *node, const float *input, double quantum_start);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_EFFECTS_H */
