/*
 * xbintsc GUI engine — the Web Audio nodes beyond the classic effect set:
 * WaveShaper, Convolver, Analyser and Panner.
 *
 * Split from `audio_mixer.cpp` for the same reason as `audio_effects.cpp`: the
 * mixer keeps graph traversal and sources, while each node's DSP lives in its
 * own place. Every render function reads the node's AudioParams at the quantum
 * start and writes the processed stereo quantum back into `node->quantum`.
 */
#ifndef XT_GUI_AUDIO_EXTRA_H
#define XT_GUI_AUDIO_EXTRA_H

#include "audio_mixer.h"

namespace xtgui {

/** Where the listener is and which way it faces, for one quantum. */
struct AudioListenerState {
  double x = 0.0, y = 0.0, z = 0.0;
  double fx = 0.0, fy = 0.0, fz = -1.0;
  double ux = 0.0, uy = 1.0, uz = 0.0;
};

/** Read the listener node's nine AudioParams at `quantum_start`. */
void listener_state_at(const AudioNode *listener, double quantum_start,
                       AudioListenerState *out);

/** WaveShaper: apply the transfer curve (with the node's oversample factor). */
void render_waveshaper(AudioNode *node, const float *input);

/** Convolver: partitioned FFT convolution with the stored impulse response. */
void render_convolver(AudioNode *node, const float *input);

/** Analyser: pass the input through and record it for the analysis getters. */
void render_analyser(AudioNode *node, const float *input);

/** Panner: equal-power panning plus distance and cone attenuation. */
void render_panner(AudioNode *node, const float *input, double quantum_start,
                   const AudioListenerState *listener);

/** Resample an impulse response to the mix rate and build its partition
 * spectra. `clip` may be null, which leaves the convolver silent. */
void prepare_convolver(AudioNode *node, const std::shared_ptr<AudioClip> &clip, bool normalize);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_EXTRA_H */
