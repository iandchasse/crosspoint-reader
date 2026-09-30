#pragma once

// SHA-256 for the card checks (P3, P8), through ESP-IDF's mbedTLS (hardware
// SHA on the S3). CROSSPOINT_TEST_SUITE builds only.
#if CROSSPOINT_TEST_SUITE
#include <mbedtls/sha256.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace testsuite {

class Sha256 {
 public:
  static constexpr size_t SIZE = 32;

  Sha256() {
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
  }
  ~Sha256() { mbedtls_sha256_free(&ctx); }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;

  // Starts a new digest.
  void restart() {
    mbedtls_sha256_free(&ctx);
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
  }
  void update(const void* data, const size_t len) {
    mbedtls_sha256_update(&ctx, static_cast<const unsigned char*>(data), len);
  }
  void finish(uint8_t (&digest)[SIZE]) { mbedtls_sha256_finish(&ctx, digest); }

  // 64 lowercase hex digits and a NUL.
  static void toHex(const uint8_t (&digest)[SIZE], char (&out)[2 * SIZE + 1]) {
    for (size_t i = 0; i < SIZE; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
  }

 private:
  mbedtls_sha256_context ctx;
};

}  // namespace testsuite
#endif
