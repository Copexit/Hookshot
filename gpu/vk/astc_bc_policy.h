// Which cards keep every large decoded ASTC texture as BC3, the finer blocks (UI, characters) too (vk_texture.cpp).
// tests/test_astc_bc_policy.py
#pragma once
#include <cstdint>
#include <cstring>

namespace astc_bc_policy {

// The VRAM the driver allows the process, in bytes: under 8.5 GB, every large ASTC texture as BC3 (a quarter of RGBA8).
// Issue 519 set it under 6 GB (an RTX 3050 Laptop, 3.3 GB allowed: 1.75 GB of decoded ASTC in Goron City, paged, 4 fps).
// 8 GB cards are allowed ~7-7.6 GB and fill up the same way: an RX 7700S (7.1 GB allowed) held 2.5 GB of decoded ASTC in
// Hyrule Castle Town's market with the VRAM at the limit; as BC3, 1.3 GB. 10 GB cards and up keep the finer ones as RGBA8
inline bool all_by_vram(uint64_t allowed) {
    return allowed && allowed < (8704ull << 20);
}

// (local, RX_VK_ASTC_BC1) Whether ASTC blocks decode with alpha 1 everywhere, read from their headers alone: every block an
// LDR void-extent block of alpha 0xFFxx, or one whose colour endpoint modes carry no alpha (4, 5, 10 and 12-15 do). Such a
// texture is kept as BC1, which is BC3's colour half: the same texels in half the bytes. The blocks are 16 bytes each in any
// order, so the block-linear storage is scanned as it is. All-zero blocks (padding) are skipped, but over half of them says
// the data is not there yet (no). A block mode the spec calls invalid counts as alpha. tests/astc_opaque_test.cpp
namespace detail {
/// The weight bits of a block mode (ASTC spec 23.17, as astcenc's decode_block_mode_2d), -1 if it is invalid or its grid is
/// larger than the block (bw x bh)
inline int weight_bits(uint32_t m, uint32_t bw, uint32_t bh, bool* dual) {
    uint32_t q = (m >> 4) & 1, H = (m >> 9) & 1, D = (m >> 10) & 1, A = (m >> 5) & 3, x, y;
    if (m & 3) {
        q |= (m & 3) << 1;
        uint32_t B = (m >> 7) & 3;
        switch ((m >> 2) & 3) {
        case 0: x = B + 4, y = A + 2; break;
        case 1: x = B + 8, y = A + 2; break;
        case 2: x = A + 2, y = B + 8; break;
        default:
            B &= 1;
            if (m & 0x100) x = B + 2, y = A + 2;
            else x = A + 2, y = B + 6;
        }
    } else {
        if (!((m >> 2) & 3)) return -1;
        q |= ((m >> 2) & 3) << 1;
        const uint32_t B = (m >> 9) & 3;
        switch ((m >> 7) & 3) {
        case 0: x = 12, y = A + 2; break;
        case 1: x = A + 2, y = 12; break;
        case 2: x = A + 6, y = B + 6, D = 0, H = 0; break;
        default:
            if ((m >> 6) & 1) return -1;   // ((m >> 5) & 3 is 2 or 3: reserved)
            x = (m >> 5) & 1 ? 10 : 6, y = (m >> 5) & 1 ? 6 : 10;
        }
    }
    const uint32_t n = x * y * (D + 1), quant = q - 2 + 6 * H;
    if (q < 2 || quant > 11 || n > 64 || x > bw || y > bh) return -1;
    // (levels 2 3 4 5 6 8 10 12 16 20 24 32: plain bits, with a trit (1) or a quint (2))
    static constexpr uint8_t bits[12] = {1, 0, 2, 0, 1, 3, 1, 2, 4, 2, 3, 5}, kind[12] = {0, 1, 0, 2, 1, 0, 2, 1, 0, 2, 1, 0};
    const uint32_t wb = n * bits[quant] + (kind[quant] == 1 ? (8 * n + 4) / 5 : kind[quant] == 2 ? (7 * n + 2) / 3 : 0);
    if (wb < 24 || wb > 96) return -1;
    *dual = D != 0;
    return (int)wb;
}
}   // namespace detail

inline bool alpha_free(const uint8_t* p, uint64_t n, uint32_t bw, uint32_t bh) {
    // (CEM 4 5 10 12 13 carry alpha; the HDR ones, 2 3 7 11 14 15, are not decoded (Eden, suyu): taken as alpha too)
    constexpr uint32_t kAlphaCems = 0xFCBC;
    uint64_t zeros = 0, blocks = n / 16;
    for (uint64_t i = 0; i < blocks; i++) {
        uint64_t lo, hi;
        std::memcpy(&lo, p + i * 16, 8);
        std::memcpy(&hi, p + i * 16 + 8, 8);
        if (!lo && !hi) { zeros++; continue; }
        const uint32_t mode = (uint32_t)lo & 0x7FF;
        if ((mode & 0x1FF) == 0x1FC) {   // (void extent: one colour, RGBA as UNORM16 in bits 64-127; HDR if bit 9)
            if ((mode & 0x600) != 0x400 || !(lo >> 11 & 1) || (hi >> 56) != 0xFF) return false;
            continue;
        }
        bool dual = false;
        const int wb = detail::weight_bits(mode, bw, bh, &dual);
        if (wb < 0) return false;
        const uint32_t parts = (uint32_t)((lo >> 11) & 3) + 1;
        if (parts == 4 && dual) return false;
        uint32_t cem[4], extra = 0;
        const uint32_t sel = (uint32_t)(lo >> 23) & 0x3F;
        if (parts == 1) {
            cem[0] = (uint32_t)(lo >> 13) & 15;
        } else if (!(sel & 3)) {   // (one mode for every partition)
            cem[0] = cem[1] = cem[2] = cem[3] = sel >> 2;
        } else {   // (each partition's: a class bit and two mode bits, the rest of them just below the weights)
            extra = 3 * parts - 4;
            const uint32_t at = 128 - (uint32_t)wb - extra;   // (24 bits of weights at least: below 104)
            const uint64_t v = at >= 64 ? hi >> (at - 64) : lo >> at | hi << (64 - at);
            const uint32_t enc = sel | (uint32_t)(v & ((1u << extra) - 1)) << 6;
            for (uint32_t k = 0; k < parts; k++)
                cem[k] = ((enc & 3) - 1 + ((enc >> (2 + k)) & 1)) * 4 + ((enc >> (2 + parts + 2 * k)) & 3);
        }
        uint32_t values = 0;
        for (uint32_t k = 0; k < parts; k++) {
            if (kAlphaCems >> cem[k] & 1) return false;
            values += (cem[k] / 4 + 1) * 2;
        }
        // (too few bits for the endpoint values, at the coarsest range: an error block, transparent black)
        const int colour_bits = 128 - wb - (parts == 1 ? 17 : 29) - (int)extra - (dual ? 2 : 0);
        if (values > 18 || colour_bits < (int)(13 * values + 4) / 5) return false;
    }
    return blocks && zeros * 2 <= blocks;
}

} // namespace astc_bc_policy
