#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct mm_sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} mm_sha256_ctx_t;

void mm_sha256_init(mm_sha256_ctx_t *ctx);
void mm_sha256_update(mm_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void mm_sha256_final(mm_sha256_ctx_t *ctx, uint8_t hash[32]);
void mm_sha256_hex(const uint8_t hash[32], char out[65]);
