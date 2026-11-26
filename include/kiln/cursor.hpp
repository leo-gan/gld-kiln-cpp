#pragma once

#include "kiln/bits.hpp"
#include "kiln/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kiln {

inline std::size_t wire_str_size(std::string_view s, std::uint64_t cap = kCap) {
    if (static_cast<std::uint64_t>(s.size()) > cap || s.size() > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    return 4 + s.size();
}

inline std::size_t wire_list_header_size(std::size_t count, std::uint64_t cap = kCap) {
    if (static_cast<std::uint64_t>(count) > cap || count > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    return 4;
}

inline std::size_t wire_pod_size(std::size_t count, std::size_t elem_width, std::uint64_t cap = kCap) {
    if (static_cast<std::uint64_t>(count) > cap || count > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    return 4 + count * elem_width;
}

// Fast positional writer into a caller-sized contiguous buffer.
class ByteWriter {
 public:
    explicit ByteWriter(std::uint8_t* dest) noexcept : start_(dest), p_(dest) {}

    std::size_t written() const noexcept { return static_cast<std::size_t>(p_ - start_); }
    std::uint8_t* cursor() const noexcept { return p_; }

    void put_bool(bool v) noexcept { *p_++ = v ? 1 : 0; }

    void put_u8(std::uint8_t v) noexcept { *p_++ = v; }

    void put_u32(std::uint32_t v) noexcept {
        store_u32(p_, v);
        p_ += 4;
    }

    void put_i32(std::int32_t v) noexcept {
        store_u32(p_, static_cast<std::uint32_t>(v));
        p_ += 4;
    }

    void put_u64(std::uint64_t v) noexcept {
        store_u64(p_, v);
        p_ += 8;
    }

    void put_i64(std::int64_t v) noexcept {
        store_u64(p_, static_cast<std::uint64_t>(v));
        p_ += 8;
    }

    void put_f32(float v) noexcept {
        store_f32(p_, v);
        p_ += 4;
    }

    void put_f64(double v) noexcept {
        store_f64(p_, v);
        p_ += 8;
    }

    void put_count(std::size_t n, std::uint64_t cap = kCap) {
        if (static_cast<std::uint64_t>(n) > cap || n > 0xFFFFFFFFu) {
            throw Error("LIMIT");
        }
        put_u32(static_cast<std::uint32_t>(n));
    }

    void put_bytes_raw(const void* data, std::size_t n) noexcept {
        if (n != 0) {
            std::memcpy(p_, data, n);
            p_ += n;
        }
    }

    void put_str(std::string_view s, std::uint64_t cap = kCap) {
        put_count(s.size(), cap);
        put_bytes_raw(s.data(), s.size());
    }

    template <typename T>
    void put_pod(const T* data, std::size_t count, std::uint64_t cap = kCap) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) == 4 || sizeof(T) == 8);
        put_count(count, cap);
        if (count != 0) {
            copy_le_from_host(p_, data, count, static_cast<int>(sizeof(T)));
            p_ += count * sizeof(T);
        }
    }

    template <typename T>
    void put_pod(const std::vector<T>& values, std::uint64_t cap = kCap) {
        put_pod(values.data(), values.size(), cap);
    }

 private:
    std::uint8_t* start_;
    std::uint8_t* p_;
};

// Checked positional reader directly over a byte span.
class ByteReader {
 public:
    explicit ByteReader(std::span<const std::uint8_t> buf, std::uint64_t cap = kCap, bool trusted = false) noexcept
        : p_(buf.data()), end_(buf.data() + buf.size()), cap_(cap), trusted_(trusted) {}

    ByteReader(const std::uint8_t* data, std::size_t size, std::uint64_t cap = kCap, bool trusted = false) noexcept
        : p_(data), end_(data + size), cap_(cap), trusted_(trusted) {}

    std::size_t left() const noexcept { return static_cast<std::size_t>(end_ - p_); }

    void need(std::size_t n) const {
        if (left() < n) {
            throw Error("EOF");
        }
    }

    std::uint8_t u8() {
        need(1);
        return *p_++;
    }

    bool boolean() {
        const std::uint8_t v = u8();
        if (v > 1) {
            throw Error("TYPE");
        }
        return v == 1;
    }

    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = load_u32(p_);
        p_ += 4;
        return v;
    }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    std::uint64_t u64() {
        need(8);
        const std::uint64_t v = load_u64(p_);
        p_ += 8;
        return v;
    }

    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

    float f32() {
        need(4);
        const float v = load_f32(p_);
        p_ += 4;
        return v;
    }

    double f64() {
        need(8);
        const double v = load_f64(p_);
        p_ += 8;
        return v;
    }

    std::uint32_t count(std::size_t each_min_bytes) {
        const std::uint32_t n = u32();
        if (static_cast<std::uint64_t>(n) > cap_) {
            throw Error("LIMIT");
        }
        if (each_min_bytes != 0) {
            if (static_cast<std::uint64_t>(n) > UINT64_MAX / each_min_bytes) {
                throw Error("LIMIT");
            }
            if (static_cast<std::uint64_t>(n) * each_min_bytes > left()) {
                throw Error("EOF");
            }
        }
        return n;
    }

    std::string_view str_view() {
        const std::uint32_t n = count(1);
        if (!trusted_ && !utf8_ok(p_, n)) {
            throw Error("UTF8");
        }
        const char* start = reinterpret_cast<const char*>(p_);
        p_ += n;
        return std::string_view(start, n);
    }

    void str_into(std::string& out) {
        const std::string_view sv = str_view();
        out.assign(sv.data(), sv.size());
    }

    std::string str() {
        const std::string_view sv = str_view();
        return std::string(sv.data(), sv.size());
    }

    template <typename T>
    void read_pod(std::vector<T>& out) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) == 4 || sizeof(T) == 8);
        const std::uint32_t n = count(sizeof(T));
        out.resize(n);
        if (n != 0) {
            copy_le_to_host(out.data(), p_, n, static_cast<int>(sizeof(T)));
            p_ += static_cast<std::size_t>(n) * sizeof(T);
        }
    }

    template <typename T>
    std::vector<T> read_pod() {
        std::vector<T> out;
        read_pod(out);
        return out;
    }

    void finish() const {
        if (p_ != end_) {
            throw Error("TRAILING");
        }
    }

 private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
    std::uint64_t cap_;
    bool trusted_;
};

}  // namespace kiln
