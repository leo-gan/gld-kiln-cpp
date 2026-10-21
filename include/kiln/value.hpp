#pragma once

#include "kiln/tensor.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln {

enum class PodKind : std::uint8_t { None, F32, F64, I32, I64, U64 };

struct PodOps;

class Value {
 public:
    enum class Tag : std::uint8_t { Null, Bool, I64, U64, F32, F64, Str, Bytes, List, Dict, Tensors, Pod };

    using List = std::vector<Value>;
    using Dict = std::vector<std::pair<std::string, Value>>;

    Value() noexcept = default;
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value();

    static Value null();
    static Value boolean(bool value);
    static Value i64(std::int64_t value);
    static Value u64(std::uint64_t value);
    static Value f32(float value);
    static Value f32_bits(std::uint32_t bits);
    static Value f64(double value);
    static Value f64_bits(std::uint64_t bits);
    static Value str(std::string value);
    static Value bytes(std::vector<std::uint8_t> value);
    static Value list(List value);
    static Value dict(Dict value);
    static Value tensors(TensorDirectory value);

    // Bulk numeric lists. The wire is u32 count plus raw little-endian elements.
    // f32 and i32 are 4 bytes. f64, i64, int, u64, and uint are 8. Views borrow.
    static Value f32s(std::vector<float> values);
    static Value f64s(std::vector<double> values);
    static Value i32s(std::vector<std::int32_t> values);
    static Value i64s(std::vector<std::int64_t> values);
    static Value u64s(std::vector<std::uint64_t> values);
    static Value f32_view(const float* data, std::size_t count);
    static Value f64_view(const double* data, std::size_t count);
    static Value i32_view(const std::int32_t* data, std::size_t count);
    static Value i64_view(const std::int64_t* data, std::size_t count);
    static Value u64_view(const std::uint64_t* data, std::size_t count);

    Tag tag() const noexcept { return tag_; }
    bool is_null() const noexcept { return tag_ == Tag::Null; }

    bool as_bool() const;
    std::int64_t as_i64() const;
    std::uint64_t as_u64() const;
    float as_f32() const;
    double as_f64() const;
    std::uint32_t f32_bits() const;
    std::uint64_t f64_bits() const;
    const std::string& as_str() const;
    const std::vector<std::uint8_t>& as_bytes() const;
    const List& as_list() const;
    const Dict& as_dict() const;
    const TensorDirectory& as_tensors() const;
    PodKind pod_kind() const noexcept;
    std::span<const float> as_f32s() const;
    std::span<const double> as_f64s() const;
    std::span<const std::int32_t> as_i32s() const;
    std::span<const std::int64_t> as_i64s() const;
    std::span<const std::uint64_t> as_u64s() const;
    const Value* find(std::string_view key) const;

    bool fit_i64(std::int64_t& out) const;
    bool fit_u64(std::uint64_t& out) const;
    Dict& reset_dict(std::size_t count);
    // Pooled dict that may still hold a previous struct's keys.
    Dict& scratch_dict();

 private:
    Tag tag_ = Tag::Null;
    std::uint64_t scalar_ = 0;
    void* ptr_ = nullptr;

    void clear() noexcept;
    void swap(Value& other) noexcept;

    friend struct PodOps;
};

template <typename T>
inline void copy_span(std::span<const T> src, std::vector<T>& out) {
    if (src.size() > out.capacity()) {
        out.reserve(src.size());
    }
    out.clear();
    if (!src.empty()) {
        out.insert(out.end(), src.data(), src.data() + src.size());
    }
}

inline void copy_f32s(const Value& value, std::vector<float>& out) { copy_span(value.as_f32s(), out); }
inline void copy_f64s(const Value& value, std::vector<double>& out) { copy_span(value.as_f64s(), out); }
inline void copy_i32s(const Value& value, std::vector<std::int32_t>& out) { copy_span(value.as_i32s(), out); }
inline void copy_i64s(const Value& value, std::vector<std::int64_t>& out) { copy_span(value.as_i64s(), out); }
inline void copy_u64s(const Value& value, std::vector<std::uint64_t>& out) { copy_span(value.as_u64s(), out); }

}  // namespace kiln
