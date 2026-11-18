/*
 * xbintsc GUI engine — a small radix-2 FFT for the audio graph.
 *
 * The convolver needs frequency-domain convolution (an impulse response is
 * seconds long, so a direct FIR would cost millions of multiply-adds per
 * 128-frame quantum) and the AnalyserNode needs a spectrum. Both work on sizes
 * that are powers of two, so one iterative in-place transform serves them.
 *
 * The buffer layout is separate real/imaginary arrays of `size` floats, which
 * is what the convolver stores anyway (it keeps spectra, not complex numbers).
 */
#ifndef XT_GUI_AUDIO_FFT_H
#define XT_GUI_AUDIO_FFT_H

#include <cmath>
#include <cstddef>
#include <vector>

namespace xtgui {

constexpr double kAudioPi = 3.14159265358979323846;

/** In-place complex FFT of `re`/`im` (both `size` long, `size` a power of two).
 *  `inverse` conjugates the twiddles and scales by 1/size. */
inline void audio_fft(std::vector<float> &re, std::vector<float> &im, bool inverse) {
  const size_t size = re.size();
  if (size < 2 || (size & (size - 1)) != 0 || im.size() != size) return;
  /* Bit-reversal permutation. */
  for (size_t i = 1, j = 0; i < size; i++) {
    size_t bit = size >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      float ti = im[i];
      im[i] = im[j];
      im[j] = ti;
    }
  }
  const double sign = inverse ? 1.0 : -1.0;
  for (size_t length = 2; length <= size; length <<= 1) {
    const double angle = sign * 2.0 * kAudioPi / static_cast<double>(length);
    const double w_re = std::cos(angle);
    const double w_im = std::sin(angle);
    for (size_t start = 0; start < size; start += length) {
      double cur_re = 1.0;
      double cur_im = 0.0;
      for (size_t k = 0; k < length / 2; k++) {
        const size_t a = start + k;
        const size_t b = a + length / 2;
        const double t_re = re[b] * cur_re - im[b] * cur_im;
        const double t_im = re[b] * cur_im + im[b] * cur_re;
        re[b] = static_cast<float>(re[a] - t_re);
        im[b] = static_cast<float>(im[a] - t_im);
        re[a] = static_cast<float>(re[a] + t_re);
        im[a] = static_cast<float>(im[a] + t_im);
        const double next_re = cur_re * w_re - cur_im * w_im;
        cur_im = cur_re * w_im + cur_im * w_re;
        cur_re = next_re;
      }
    }
  }
  if (inverse) {
    const float scale = 1.0f / static_cast<float>(size);
    for (size_t i = 0; i < size; i++) {
      re[i] *= scale;
      im[i] *= scale;
    }
  }
}

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_FFT_H */
