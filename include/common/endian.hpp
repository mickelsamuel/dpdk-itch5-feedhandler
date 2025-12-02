#pragma once

#include <cstdint>
#include <cstring>

#if defined(__linux__)
    #include <endian.h>
#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>
    #define be16toh(x) OSSwapBigToHostInt16(x)
    #define be32toh(x) OSSwapBigToHostInt32(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
    #define htobe16(x) OSSwapHostToBigInt16(x)
    #define htobe32(x) OSSwapHostToBigInt32(x)
    #define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(_WIN32)
    #include <stdlib.h>
    #define be16toh(x) _byteswap_ushort(x)
    #define be32toh(x) _byteswap_ulong(x)
    #define be64toh(x) _byteswap_uint64(x)
    #define htobe16(x) _byteswap_ushort(x)
    #define htobe32(x) _byteswap_ulong(x)
    #define htobe64(x) _byteswap_uint64(x)
#endif

namespace hft {
namespace endian {

// Efficient byte swapping using compiler intrinsics
// These map to single CPU instructions on modern processors (BSWAP)

inline uint16_t swap16(uint16_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(val);
#else
    return be16toh(val);
#endif
}

inline uint32_t swap32(uint32_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return be32toh(val);
#endif
}

inline uint64_t swap64(uint64_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#else
    return be64toh(val);
#endif
}

// Network (Big Endian) to Host byte order conversion
// x86/x64 uses Little Endian, so we always need to swap

inline uint16_t ntoh16(uint16_t net) noexcept {
    return swap16(net);
}

inline uint32_t ntoh32(uint32_t net) noexcept {
    return swap32(net);
}

inline uint64_t ntoh64(uint64_t net) noexcept {
    return swap64(net);
}

// Host to Network byte order conversion
inline uint16_t hton16(uint16_t host) noexcept {
    return swap16(host);
}

inline uint32_t hton32(uint32_t host) noexcept {
    return swap32(host);
}

inline uint64_t hton64(uint64_t host) noexcept {
    return swap64(host);
}

// Read big-endian values from unaligned memory (zero-copy safe)
inline uint16_t read_be16(const void* ptr) noexcept {
    uint16_t val;
    std::memcpy(&val, ptr, sizeof(val));
    return swap16(val);
}

inline uint32_t read_be32(const void* ptr) noexcept {
    uint32_t val;
    std::memcpy(&val, ptr, sizeof(val));
    return swap32(val);
}

inline uint64_t read_be64(const void* ptr) noexcept {
    uint64_t val;
    std::memcpy(&val, ptr, sizeof(val));
    return swap64(val);
}

// Read 6-byte big-endian timestamp (ITCH uses 6-byte timestamps)
inline uint64_t read_be48(const void* ptr) noexcept {
    const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
    return (static_cast<uint64_t>(bytes[0]) << 40) |
           (static_cast<uint64_t>(bytes[1]) << 32) |
           (static_cast<uint64_t>(bytes[2]) << 24) |
           (static_cast<uint64_t>(bytes[3]) << 16) |
           (static_cast<uint64_t>(bytes[4]) << 8)  |
           (static_cast<uint64_t>(bytes[5]));
}

} // namespace endian
} // namespace hft
