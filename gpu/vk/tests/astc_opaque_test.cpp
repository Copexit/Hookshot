// Game-free check of astc_bc_policy::alpha_free (RX_VK_ASTC_BC1) against suyu's CPU ASTC decoder: random blocks, and every
// block it calls alpha-free must decode with alpha 255 in every texel (random blocks are mostly invalid: ~2% pass). Build
// from the repo root: g++ -std=c++20 -O2 -DNDEBUG -Igpu/compat -Igpu/third_party/suyu gpu/vk/tests/astc_opaque_test.cpp
// gpu/third_party/suyu/video_core/textures/astc.cpp -o /tmp/astc_opaque_test -lpthread && /tmp/astc_opaque_test
#include <cstdio>
#include <random>
#include <span>

#include "../astc_bc_policy.h"
#include "video_core/textures/astc.h"

int main() {
    std::mt19937_64 rng(1);
    int failures = 0, opaque = 0, alpha = 0, total = 0;
    const int dims[][2] = {{4, 4}, {5, 5}, {6, 6}, {8, 8}, {10, 10}, {12, 12}};
    for (int it = 0; it < 400000; it++) {
        uint8_t b[16];
        const uint64_t lo = rng(), hi = rng();
        std::memcpy(b, &lo, 8), std::memcpy(b + 8, &hi, 8);
        if (it & 1) b[0] = 0xFC, b[1] = (uint8_t)((b[1] & 0xF8) | 0x05 | (rng() & 2));   // (void extent, LDR or HDR)
        const auto& d = dims[it % 6];
        uint8_t out[12 * 12 * 4];
        Tegra::Texture::ASTC::DecompressSerial(std::span<const uint8_t>(b, 16), d[0], d[1], 1, d[0], d[1],
                                               std::span<uint8_t>(out, d[0] * d[1] * 4));
        bool has_alpha = false;
        for (int i = 0; i < d[0] * d[1]; i++) has_alpha |= out[i * 4 + 3] != 255;
        total++;
        alpha += has_alpha;
        if (!astc_bc_policy::alpha_free(b, 16, d[0], d[1])) continue;
        opaque++;
        if (has_alpha && failures++ < 10)
            std::fprintf(stderr, "block %016llx%016llx (%dx%d): called alpha-free, decodes with alpha\n",
                         (unsigned long long)hi, (unsigned long long)lo, d[0], d[1]);
    }
    // (padding: zeros skipped, but mostly zeros is "not loaded yet")
    uint8_t z[64]{};
    if (astc_bc_policy::alpha_free(z, 64, 4, 4)) std::fprintf(stderr, "all-zero data called alpha-free\n"), failures++;
    std::printf("%d blocks: %d decode with alpha, %d called alpha-free; %d wrong\n", total, alpha, opaque, failures);
    if (opaque < total / 100) std::fprintf(stderr, "too few called alpha-free: the check is not exercised\n"), failures++;
    return failures != 0;
}
