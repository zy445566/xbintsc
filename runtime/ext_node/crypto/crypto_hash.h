#ifndef XT_CRYPTO_HASH_H
#define XT_CRYPTO_HASH_H

#include <stddef.h>
#include <stdint.h>

/* SHA-1 / SHA-256 over one buffer. Returns the digest length, or 0 for an
 * unsupported algorithm. Shared by `crypto.c` (createHash) and
 * `crypto_random.c` (PBKDF2). */
size_t xt_crypto_hash_compute(const char *algorithm, const uint8_t *data, size_t length,
                              uint8_t *out);

#endif /* XT_CRYPTO_HASH_H */
