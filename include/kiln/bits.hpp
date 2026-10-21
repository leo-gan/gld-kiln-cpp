#pragma once

#include "kiln/error.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace kiln {

constexpr std::uint64_t kCap = 16777216;
constexpr std::size_t kHeader = 64;
constexpr std::size_t kDesc = 128;
constexpr int kMaxDepth = 64;

inline std::uint64_t add_u64(std::uint64_t a, std::uint64_t b) {
    if (a > UINT64_MAX - b) {
        throw Error("LIMIT");
    }
    return a + b;
}

inline std::uint64_t mul_u64(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a) {
        throw Error("LIMIT");
    }
    return a * b;
}

inline std::uint64_t align64(std::uint64_t n) {
    if (n > UINT64_MAX - 63) {
        throw Error("LIMIT");
    }
    return (n + 63) & ~std::uint64_t{63};
}

inline void fit_count(std::uint64_t count, std::uint64_t each, std::uint64_t unread, std::uint64_t cap) {
    if (count > cap) {
        throw Error("LIMIT");
    }
    if (each > 0 && count > UINT64_MAX / each) {
        throw Error("LIMIT");
    }
    if (each > 0 && count > unread / each) {
        throw Error("EOF");
    }
}

inline bool utf8_ok(const std::uint8_t* s, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t c = s[i];
        if (c <= 0x7F) {
            ++i;
            continue;
        }
        std::size_t need = 0;
        std::uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            need = 2;
            cp = c & 0x1F;
            if (c < 0xC2) {
                return false;
            }
        } else if ((c & 0xF0) == 0xE0) {
            need = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            need = 4;
            cp = c & 0x07;
            if (c > 0xF4) {
                return false;
            }
        } else {
            return false;
        }
        if (i + need > n) {
            return false;
        }
        for (std::size_t j = 1; j < need; ++j) {
            if ((s[i + j] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (s[i + j] & 0x3F);
        }
        if (need == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) {
            return false;
        }
        if (need == 4 && (cp < 0x10000 || cp > 0x10FFFF)) {
            return false;
        }
        i += need;
    }
    return true;
}

inline bool utf8_ok(std::string_view s) {
    return utf8_ok(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

inline std::uint16_t load_u16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = static_cast<std::uint16_t>((v >> 8) | (v << 8));
#endif
    return v;
}

inline std::uint32_t load_u32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

inline std::uint64_t load_u64(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

inline void store_u16(std::uint8_t* p, std::uint16_t v) {
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = static_cast<std::uint16_t>((v >> 8) | (v << 8));
#endif
    std::memcpy(p, &v, 2);
}

inline void store_u32(std::uint8_t* p, std::uint32_t v) {
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    std::memcpy(p, &v, 4);
}

inline void store_u64(std::uint8_t* p, std::uint64_t v) {
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    std::memcpy(p, &v, 8);
}

inline void store_f32(std::uint8_t* p, float v) {
    static_assert(sizeof(float) == 4, "float is 32-bit");
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    store_u32(p, bits);
}

inline void store_f64(std::uint8_t* p, double v) {
    static_assert(sizeof(double) == 8, "double is 64-bit");
    std::uint64_t bits;
    std::memcpy(&bits, &v, 8);
    store_u64(p, bits);
}

inline float load_f32(const std::uint8_t* p) {
    std::uint32_t bits = load_u32(p);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

inline double load_f64(const std::uint8_t* p) {
    std::uint64_t bits = load_u64(p);
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}

// Host arrays of width 4 or 8 match the wire on a little-endian machine.
// Big-endian hosts swap each element. Callers must pass the real element width:
// f32 and i32 are 4 bytes; f64, i64, int, u64, and uint are 8. u32 is not 8.
inline void copy_le_from_host(std::uint8_t* dest, const void* src, std::size_t count, int width) {
    if (count == 0) {
        return;
    }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(dest, src, count * static_cast<std::size_t>(width));
#else
    const auto* in = static_cast<const std::uint8_t*>(src);
    if (width == 4) {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, in + i * 4, 4);
            store_u32(dest + i * 4, bits);
        }
        return;
    }
    if (width == 8) {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, in + i * 8, 8);
            store_u64(dest + i * 8, bits);
        }
        return;
    }
    throw Error("TYPE");
#endif
}

inline void copy_le_to_host(void* dest, const std::uint8_t* src, std::size_t count, int width) {
    if (count == 0) {
        return;
    }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(dest, src, count * static_cast<std::size_t>(width));
#else
    auto* out = static_cast<std::uint8_t*>(dest);
    if (width == 4) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t bits = load_u32(src + i * 4);
            std::memcpy(out + i * 4, &bits, 4);
        }
        return;
    }
    if (width == 8) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint64_t bits = load_u64(src + i * 8);
            std::memcpy(out + i * 8, &bits, 8);
        }
        return;
    }
    throw Error("TYPE");
#endif
}

}  // namespace kiln
