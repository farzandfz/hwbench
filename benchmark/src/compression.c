#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/*
 * Simple LZ77-style compressor implemented inline.
 * We use a sliding window of 4096 bytes and search for matches.
 * This is representative of real compression work (memory + branch heavy)
 * without requiring an external library.
 */

/* Small window to keep O(n*window) tractable for throughput measurement */
#define WINDOW_BITS   9
#define WINDOW_SIZE  (1 << WINDOW_BITS)    /* 512 */
#define MIN_MATCH     3
#define MAX_MATCH    18

typedef struct {
    uint16_t distance; /* 0 = literal */
    uint8_t  length;   /* if distance==0: literal byte in 'lit'; else match len */
    uint8_t  lit;
} Token;

/* Returns number of tokens written */
static size_t lz_compress(const uint8_t *src, size_t src_len,
                           Token *tokens, size_t max_tokens) {
    size_t pos = 0, ntok = 0;
    while (pos < src_len && ntok < max_tokens) {
        /* Search backwards for longest match */
        int best_len  = 0;
        int best_dist = 0;
        size_t win_start = pos > WINDOW_SIZE ? pos - WINDOW_SIZE : 0;
        for (size_t s = win_start; s < pos; s++) {
            int mlen = 0;
            while (mlen < MAX_MATCH &&
                   pos + mlen < src_len &&
                   src[s + mlen] == src[pos + mlen])
                mlen++;
            if (mlen > best_len) {
                best_len  = mlen;
                best_dist = (int)(pos - s);
            }
        }
        if (best_len >= MIN_MATCH) {
            tokens[ntok++] = (Token){(uint16_t)best_dist,
                                     (uint8_t)best_len, 0};
            pos += (size_t)best_len;
        } else {
            tokens[ntok++] = (Token){0, 0, src[pos]};
            pos++;
        }
    }
    return ntok;
}

static size_t lz_decompress(const Token *tokens, size_t ntok,
                              uint8_t *out, size_t out_cap) {
    size_t pos = 0;
    for (size_t i = 0; i < ntok && pos < out_cap; i++) {
        if (tokens[i].distance == 0) {
            out[pos++] = tokens[i].lit;
        } else {
            size_t start = pos - tokens[i].distance;
            size_t len   = tokens[i].length;
            for (size_t k = 0; k < len && pos < out_cap; k++)
                out[pos++] = out[start + k];
        }
    }
    return pos;
}

#define INPUT_MB 4

CompressionResult bench_compression(const BenchConfig *cfg) {
    CompressionResult r = {0};
    r.input_size_mb = INPUT_MB;

    size_t in_bytes = (size_t)INPUT_MB * 1024 * 1024;
    /* max tokens: one per byte in worst case */
    size_t max_tok  = in_bytes;

    uint8_t *input   = malloc(in_bytes);
    Token   *tokens  = malloc(max_tok * sizeof(Token));
    uint8_t *output  = malloc(in_bytes);

    if (!input || !tokens || !output) {
        free(input); free(tokens); free(output);
        r.skipped = true;
        return r;
    }

    /* Generate deterministic input with some entropy + repetition */
    for (size_t i = 0; i < in_bytes; i++) {
        uint32_t x = (uint32_t)i;
        x ^= x >> 17;
        x *= 0xbf324c81u;
        x ^= x >> 13;
        /* mix repetition: every 64 bytes repeat the last 32 */
        if ((i & 63) >= 32)
            input[i] = input[i - 32] ^ (uint8_t)(x & 0x0F);
        else
            input[i] = (uint8_t)(x & 0xFF);
    }

    struct timespec ts;
    uint64_t compress_iters   = 0;
    uint64_t decompress_iters = 0;
    size_t   ntok             = 0;

    /* ---- Compression: run for duration_sec ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (elapsed_sec(&ts) < (double)cfg->duration_sec) {
        ntok = lz_compress(input, in_bytes, tokens, max_tok);
        compress_iters++;
        COMPILER_BARRIER(ntok);
    }
    double compress_t = elapsed_sec(&ts);
    r.compress_mbps = (double)compress_iters * INPUT_MB / compress_t;

    /* compression ratio from last run */
    /* each token is 4 bytes, input is in_bytes */
    r.compression_ratio = (double)in_bytes / ((double)ntok * sizeof(Token));

    /* ---- Decompression: run for duration_sec ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (elapsed_sec(&ts) < (double)cfg->duration_sec) {
        size_t got = lz_decompress(tokens, ntok, output, in_bytes);
        decompress_iters++;
        COMPILER_BARRIER(got);
    }
    double decompress_t = elapsed_sec(&ts);
    r.decompress_mbps = (double)decompress_iters * INPUT_MB / decompress_t;

    free(input);
    free(tokens);
    free(output);
    return r;
}
