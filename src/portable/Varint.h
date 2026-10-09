// Fixed-width reimplementation of the RDL2 ValueContainer variable-length
// integer codec (scene_rdl2 lib/scene/rdl2/ValueContainerUtil.h @ d3da034).
//
// Wire format (verified against pinned upstream source):
//   - LEB128, little-endian base-128: byte N carries value bits [7N, 7N+6];
//     bit 7 of each byte is the continuation flag (1 = more bytes follow).
//     Zero encodes as the single byte 0x00.
//   - Signed values are zig-zag mapped first:
//       enc(i) = (i >> (w-1)) ^ (i << 1)   with ARITHMETIC right shift
//       dec(u) = (u >> 1) ^ -(u & 1)
//   - Max encoded sizes: 5 bytes (32-bit), 10 bytes (64-bit).
//
// Upstream types the codec on `long`/`unsigned long`, which silently truncates
// and UB-shifts on LLP64 Windows (HISTORICAL_WINDOWS_PORT_AUDIT.md §1 item
// #27-30; Milestone 2 phase 2). This implementation is typed on
// int64_t/uint64_t — byte-identical to upstream's LP64 behavior — and is the
// reference for the M2 scene_rdl2 patch plus its cross-platform tests.

#pragma once

#include <cstddef>
#include <cstdint>

namespace openmoonray_anari {
namespace portable {

constexpr std::size_t kVarintMaxSize32 = 5;
constexpr std::size_t kVarintMaxSize64 = 10;

inline uint32_t zigZagEncode32(int32_t i)
{
    return static_cast<uint32_t>(i >> 31) ^ (static_cast<uint32_t>(i) << 1);
}

inline int32_t zigZagDecode32(uint32_t u)
{
    return static_cast<int32_t>((u >> 1) ^ (0u - (u & 1u)));
}

inline uint64_t zigZagEncode64(int64_t i)
{
    return static_cast<uint64_t>(i >> 63) ^ (static_cast<uint64_t>(i) << 1);
}

inline int64_t zigZagDecode64(uint64_t u)
{
    return static_cast<int64_t>((u >> 1) ^ (0ull - (u & 1ull)));
}

// --- unsigned encode/decode -------------------------------------------------

inline std::size_t encodeVarint64(uint64_t value, unsigned char *out)
{
    std::size_t size = 0;
    while (value > 0x7Full) {
        out[size++] = static_cast<unsigned char>((value & 0x7Full) | 0x80ull);
        value >>= 7;
    }
    out[size++] = static_cast<unsigned char>(value);
    return size;
}

// Decodes at most maxBytes; returns bytes consumed, 0 on malformed/truncated
// input (upstream trusts the stream — the bound check here is an intentional
// hardening the M2 patch should carry too).
inline std::size_t decodeVarint64(const unsigned char *in, std::size_t maxBytes,
                                  uint64_t &value)
{
    value = 0;
    std::size_t size = 0;
    unsigned shift = 0;
    while (size < maxBytes && size < kVarintMaxSize64) {
        const unsigned char byte = in[size++];
        value |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            return size;
        }
        shift += 7;
    }
    return 0;
}

inline std::size_t encodedVarintSize64(uint64_t value)
{
    std::size_t size = 1;
    while (value > 0x7Full) {
        value >>= 7;
        ++size;
    }
    return size;
}

inline std::size_t encodeVarint32(uint32_t value, unsigned char *out)
{
    return encodeVarint64(value, out);
}

inline std::size_t decodeVarint32(const unsigned char *in, std::size_t maxBytes,
                                  uint32_t &value)
{
    uint64_t wide = 0;
    const std::size_t size = decodeVarint64(in, maxBytes, wide);
    value = static_cast<uint32_t>(wide);
    return size;
}

// --- signed convenience wrappers --------------------------------------------

inline std::size_t encodeVarintSigned32(int32_t value, unsigned char *out)
{
    return encodeVarint32(zigZagEncode32(value), out);
}

inline std::size_t decodeVarintSigned32(const unsigned char *in, std::size_t maxBytes,
                                        int32_t &value)
{
    uint32_t u = 0;
    const std::size_t size = decodeVarint32(in, maxBytes, u);
    value = zigZagDecode32(u);
    return size;
}

inline std::size_t encodeVarintSigned64(int64_t value, unsigned char *out)
{
    return encodeVarint64(zigZagEncode64(value), out);
}

inline std::size_t decodeVarintSigned64(const unsigned char *in, std::size_t maxBytes,
                                        int64_t &value)
{
    uint64_t u = 0;
    const std::size_t size = decodeVarint64(in, maxBytes, u);
    value = zigZagDecode64(u);
    return size;
}

} // namespace portable
} // namespace openmoonray_anari
