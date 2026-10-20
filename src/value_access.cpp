#include "kiln/value.hpp"

#include "pool.hpp"

#include "kiln/error.hpp"

#include <cstring>

namespace kiln {

bool Value::as_bool() const {
    if (tag_ != Tag::Bool) {
        throw Error("TYPE");
    }
    return scalar_ != 0;
}

std::int64_t Value::as_i64() const {
    if (tag_ != Tag::I64) {
        throw Error("TYPE");
    }
    std::int64_t value = 0;
    std::memcpy(&value, &scalar_, 8);
    return value;
}

std::uint64_t Value::as_u64() const {
    if (tag_ != Tag::U64) {
        throw Error("TYPE");
    }
    return scalar_;
}

std::uint32_t Value::f32_bits() const {
    if (tag_ != Tag::F32) {
        throw Error("TYPE");
    }
    return static_cast<std::uint32_t>(scalar_);
}

float Value::as_f32() const {
    const std::uint32_t bits = f32_bits();
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

std::uint64_t Value::f64_bits() const {
    if (tag_ != Tag::F64) {
        throw Error("TYPE");
    }
    return scalar_;
}

double Value::as_f64() const {
    const std::uint64_t bits = f64_bits();
    double value = 0;
    std::memcpy(&value, &bits, 8);
    return value;
}

const std::string& Value::as_str() const {
    if (tag_ != Tag::Str) {
        throw Error("TYPE");
    }
    return *static_cast<const std::string*>(ptr_);
}

const std::vector<std::uint8_t>& Value::as_bytes() const {
    if (tag_ != Tag::Bytes) {
        throw Error("TYPE");
    }
    return *static_cast<const std::vector<std::uint8_t>*>(ptr_);
}

const Value::List& Value::as_list() const {
    if (tag_ != Tag::List) {
        throw Error("TYPE");
    }
    return *static_cast<const List*>(ptr_);
}

const Value::Dict& Value::as_dict() const {
    if (tag_ != Tag::Dict) {
        throw Error("TYPE");
    }
    return *static_cast<const Dict*>(ptr_);
}

const TensorDirectory& Value::as_tensors() const {
    if (tag_ != Tag::Tensors) {
        throw Error("TYPE");
    }
    return *static_cast<const TensorDirectory*>(ptr_);
}

const Value* Value::find(std::string_view key) const {
    if (tag_ != Tag::Dict) {
        return nullptr;
    }
    for (const auto& item : as_dict()) {
        if (item.first == key) {
            return &item.second;
        }
    }
    return nullptr;
}

bool Value::fit_i64(std::int64_t& out) const {
    if (tag_ == Tag::I64) {
        out = as_i64();
        return true;
    }
    if (tag_ == Tag::U64 && scalar_ <= static_cast<std::uint64_t>(INT64_MAX)) {
        out = static_cast<std::int64_t>(scalar_);
        return true;
    }
    return false;
}

Value::Dict& Value::scratch_dict() {
    if (tag_ == Tag::Dict) {
        return *static_cast<Dict*>(ptr_);
    }
    clear();
    Dict* dict = Pool<Dict>::take();
    tag_ = Tag::Dict;
    ptr_ = dict;
    return *dict;
}

bool Value::fit_u64(std::uint64_t& out) const {
    if (tag_ == Tag::U64) {
        out = scalar_;
        return true;
    }
    if (tag_ == Tag::I64) {
        const std::int64_t value = as_i64();
        if (value < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(value);
        return true;
    }
    return false;
}

}  // namespace kiln
