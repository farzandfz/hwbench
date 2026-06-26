#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/* ---- SHA-256 (FIPS 180-4) ---- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(e,f,g)    (((e) & (f)) ^ (~(e) & (g)))
#define MAJ(a,b,c)   (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))
#define EP0(a)  (ROTR32(a,2)  ^ ROTR32(a,13) ^ ROTR32(a,22))
#define EP1(e)  (ROTR32(e,6)  ^ ROTR32(e,11) ^ ROTR32(e,25))
#define SIG0(x) (ROTR32(x,7)  ^ ROTR32(x,18) ^ ((x) >>  3))
#define SIG1(x) (ROTR32(x,17) ^ ROTR32(x,19) ^ ((x) >> 10))

static void sha256_block(uint32_t hash[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]   << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |
               ((uint32_t)block[i*4+3]);
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    uint32_t a=hash[0], b=hash[1], c=hash[2], d=hash[3];
    uint32_t e=hash[4], f=hash[5], g=hash[6], h=hash[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e,f,g) + K[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }
    hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d;
    hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
}

/* Hash 'len' bytes of 'data'; output in 'digest' (32 bytes) */
static void sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    uint32_t hash[8];
    memcpy(hash, H0, sizeof(H0));

    uint8_t block[64];
    size_t  pos = 0;

    while (pos + 64 <= len) {
        sha256_block(hash, data + pos);
        pos += 64;
    }

    /* final block(s) with padding */
    size_t rem = len - pos;
    memset(block, 0, 64);
    memcpy(block, data + pos, rem);
    block[rem] = 0x80;

    if (rem >= 56) {
        sha256_block(hash, block);
        memset(block, 0, 64);
    }
    /* length in bits as 64-bit big-endian */
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        block[63-i] = (uint8_t)(bit_len >> (8*i));
    sha256_block(hash, block);

    for (int i = 0; i < 8; i++) {
        digest[i*4  ] = (uint8_t)(hash[i] >> 24);
        digest[i*4+1] = (uint8_t)(hash[i] >> 16);
        digest[i*4+2] = (uint8_t)(hash[i] >>  8);
        digest[i*4+3] = (uint8_t)(hash[i]);
    }
}

#define HASH_BUF_MB 16

HashingResult bench_hashing(const BenchConfig *cfg) {
    size_t buf_bytes = (size_t)HASH_BUF_MB * 1024 * 1024;
    uint8_t *buf = malloc(buf_bytes);
    if (!buf) return (HashingResult){0};

    /* deterministic input */
    for (size_t i = 0; i < buf_bytes; i++)
        buf[i] = (uint8_t)(i ^ (i >> 8));

    uint8_t digest[32];
    uint64_t hashes = 0;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (elapsed_sec(&ts) < (double)cfg->duration_sec) {
        sha256(buf, buf_bytes, digest);
        hashes++;
        COMPILER_BARRIER(digest[0]);
    }
    double t = elapsed_sec(&ts);

    free(buf);

    HashingResult r;
    r.hashes_per_sec  = (double)hashes / t;
    r.throughput_mbps = (double)hashes * HASH_BUF_MB / t;
    return r;
}
