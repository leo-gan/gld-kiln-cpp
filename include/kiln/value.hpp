#pragma once

#include "kiln/tensor.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln {

class Value {
 public:
    enum class Tag : std::uint8_t { Null, Bool, I64, U64, F32, F64, Str, Bytes, List, Dict, Tensors };

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
};

}  // namespace kiln
