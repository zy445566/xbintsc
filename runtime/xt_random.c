/*
 * xbintsc runtime — OS entropy and the process-wide PRNG.
 *
 * `Math.random` used to be `rand() / (RAND_MAX + 1)`, which on the MSVC CRT has
 * only 32 767 output states: salts and nonces derived from it collided and were
 * predictable. This unit reads OS entropy (`BCryptGenRandom` on Windows,
 * `/dev/urandom` elsewhere) for the CSPRNG surface and seeds a xoshiro256**
 * generator for `Math.random`, so every run produces a different, well-mixed
 * stream.
 */

#include "rt.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdlib.h>
#endif
#endif

static uint64_t xt_random_state[4];
static volatile int xt_random_seeded = 0;

/** SplitMix64, used to expand the seed material and to avoid an all-zero
 * xoshiro state. */
static uint64_t xt_splitmix64(uint64_t *state) {
  uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

static uint64_t xt_rotate_left(uint64_t value, int bits) {
  return (value << bits) | (value >> (64 - bits));
}

int xt_secure_random(uint8_t *out, size_t length) {
  if (length == 0) return 0;
  if (out == NULL) return -1;
#if defined(_WIN32)
  NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)out, (ULONG)length,
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return status == 0 ? 0 : -1;
#elif defined(__APPLE__)
  arc4random_buf(out, length);
  return 0;
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -1;
  size_t offset = 0;
  while (offset < length) {
    ssize_t got = read(fd, out + offset, length - offset);
    if (got <= 0) {
      if (got < 0 && errno == EINTR) continue;
      close(fd);
      return -1;
    }
    offset += (size_t)got;
  }
  close(fd);
  return 0;
#endif
}

/** Fill four 64-bit words with seed material. The clock, the process id, the
 * address of a static and a counter are mixed in even when the OS CSPRNG
 * works, so two processes started in the same nanosecond cannot share a
 * stream. */
static void xt_random_fill_seed(uint64_t seed[4]) {
  uint8_t bytes[32];
  uint64_t material[4];
  if (xt_secure_random(bytes, sizeof(bytes)) == 0) {
    memcpy(material, bytes, sizeof(material));
  } else {
    material[0] = (uint64_t)time(NULL);
    material[1] = (uint64_t)clock();
#if defined(_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    material[2] = (uint64_t)counter.QuadPart;
    material[3] = (uint64_t)GetCurrentProcessId();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    material[2] = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    material[3] = (uint64_t)getpid();
#endif
  }
  material[0] ^= (uint64_t)(uintptr_t)&xt_random_state;
  material[1] ^= (uint64_t)(uintptr_t)&seed;
  uint64_t state = material[0] ^ xt_rotate_left(material[1], 17);
  for (int i = 0; i < 4; i++) {
    seed[i] = xt_splitmix64(&state);
    seed[i] ^= material[i];
    seed[i] = xt_splitmix64(&seed[i]);
  }
  if ((seed[0] | seed[1] | seed[2] | seed[3]) == 0) seed[0] = 0x9E3779B97F4A7C15ULL;
}

static void xt_random_seed(void) {
  uint64_t seed[4];
  xt_random_fill_seed(seed);
  memcpy(xt_random_state, seed, sizeof(seed));
  xt_random_seeded = 1;
}

uint64_t xt_random_u64(void) {
  if (!xt_random_seeded) xt_random_seed();
  uint64_t *s = xt_random_state;
  uint64_t result = xt_rotate_left(s[1] * 5, 7) * 9;
  uint64_t t = s[1] << 17;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = xt_rotate_left(s[3], 45);
  return result;
}

double xt_random_double(void) {
  /* 53 random bits: every double in [0, 1) that JavaScript can represent. */
  return (double)(xt_random_u64() >> 11) * (1.0 / 9007199254740992.0);
}

uint64_t xt_random_below(uint64_t bound) {
  if (bound == 0) return 0;
  /* Rejection sampling removes the modulo bias of `% bound`. */
  uint64_t threshold = (uint64_t)(-bound) % bound;
  for (;;) {
    uint64_t value = xt_random_u64();
    if (value >= threshold) return value % bound;
  }
}
