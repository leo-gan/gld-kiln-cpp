#pragma once

#include "kiln/json.hpp"
#include "kiln/tensor.hpp"
#include "kiln/value.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace kiln::test {

inline int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

inline std::vector<std::uint8_t> unhex(const std::string& text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((hex_nibble(text[i]) << 4) | hex_nibble(text[i + 1])));
    }
    return out;
}

inline std::string hex_of(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out(size * 2, ' ');
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

inline json::Value number_i(std::int64_t value) {
    json::Value out;
    out.type = json::Value::Type::Number;
    out.is_int = true;
    out.i = value;
    out.d = static_cast<double>(value);
    if (value >= 0) {
        out.is_uint = true;
        out.u = static_cast<std::uint64_t>(value);
    }
    return out;
}

inline json::Value number_u(std::uint64_t value) {
    json::Value out;
    out.type = json::Value::Type::Number;
    out.is_uint = true;
    out.u = value;
    out.d = static_cast<double>(value);
    if (value <= static_cast<std::uint64_t>(INT64_MAX)) {
        out.is_int = true;
        out.i = static_cast<std::int64_t>(value);
    }
    return out;
}

inline json::Value project_directory(const TensorDirectory& directory) {
    json::Value out;
    out.type = json::Value::Type::Object;
    json::Value flags;
    flags.type = json::Value::Type::Array;
    if (directory.has_names) {
        json::Value name;
        name.type = json::Value::Type::String;
        name.s = "names";
        flags.a.push_back(name);
    }
    json::Value le;
    le.type = json::Value::Type::String;
    le.s = "le";
    flags.a.push_back(std::move(le));
    json::Value tensors;
    tensors.type = json::Value::Type::Array;
    for (const Tensor& tensor : directory.tensors) {
        json::Value item;
        item.type = json::Value::Type::Object;
        json::Value name;
        if (tensor.name && !tensor.name->empty()) {
            name.type = json::Value::Type::String;
            name.s = *tensor.name;
        } else {
            name.type = json::Value::Type::Null;
        }
        json::Value dtype;
        if (const char* label = dtype_name(tensor.dtype)) {
            dtype.type = json::Value::Type::String;
            dtype.s = label;
        } else {
            dtype = number_u(tensor.dtype);
        }
        json::Value shape;
        shape.type = json::Value::Type::Array;
        for (std::uint8_t dim = 0; dim < tensor.rank; ++dim) {
            shape.a.push_back(number_u(tensor.shape[dim]));
        }
        json::Value data;
        data.type = json::Value::Type::String;
        data.s = hex_of(tensor.data.data, tensor.data.size);
        item.o.emplace_back("name", std::move(name));
        item.o.emplace_back("dtype", std::move(dtype));
        item.o.emplace_back("shape", std::move(shape));
        item.o.emplace_back("quant_scheme", number_u(tensor.quant_scheme));
        item.o.emplace_back("data_hex", std::move(data));
        tensors.a.push_back(std::move(item));
    }
    out.o.emplace_back("version", number_i(directory.version));
    out.o.emplace_back("flags", std::move(flags));
    out.o.emplace_back("tensors", std::move(tensors));
    out.o.emplace_back("block_length", number_u(directory.block_length));
    return out;
}

inline json::Value project(const Value& value) {
    switch (value.tag()) {
        case Value::Tag::Null: {
            json::Value out;
            out.type = json::Value::Type::Null;
            return out;
        }
        case Value::Tag::Bool: {
            json::Value out;
            out.type = json::Value::Type::Bool;
            out.b = value.as_bool();
            return out;
        }
        case Value::Tag::I64:
            return number_i(value.as_i64());
        case Value::Tag::U64:
            return number_u(value.as_u64());
        case Value::Tag::F32: {
            json::Value out;
            out.type = json::Value::Type::Number;
            out.d = value.as_f32();
            return out;
        }
        case Value::Tag::F64: {
            json::Value out;
            out.type = json::Value::Type::Number;
            out.d = value.as_f64();
            return out;
        }
        case Value::Tag::Str: {
            json::Value out;
            out.type = json::Value::Type::String;
            out.s = value.as_str();
            return out;
        }
        case Value::Tag::List: {
            json::Value out;
            out.type = json::Value::Type::Array;
            for (const Value& item : value.as_list()) {
                out.a.push_back(project(item));
            }
            return out;
        }
        case Value::Tag::Dict: {
            json::Value out;
            out.type = json::Value::Type::Object;
            for (const auto& item : value.as_dict()) {
                out.o.emplace_back(item.first, project(item.second));
            }
            return out;
        }
        case Value::Tag::Tensors:
            return project_directory(value.as_tensors());
        default: {
            json::Value out;
            out.type = json::Value::Type::Null;
            return out;
        }
    }
}

inline bool same_number(const json::Value& got, const json::Value& want) {
    if (std::isnan(want.d) && std::isnan(got.d)) {
        return true;
    }
    if ((want.is_int || want.is_uint) && (got.is_int || got.is_uint)) {
        if (want.is_uint && !want.is_int) {
            return got.is_uint && got.u == want.u;
        }
        if (got.is_int && want.is_int) {
            return got.i == want.i;
        }
        if (got.is_uint && want.is_int && want.i >= 0) {
            return got.u == static_cast<std::uint64_t>(want.i);
        }
    }
    return got.d == want.d;
}

inline bool same(const json::Value& got, const json::Value& want) {
    if (want.type == json::Value::Type::Number && got.type == json::Value::Type::Number) {
        return same_number(got, want);
    }
    if (want.type == json::Value::Type::Object && got.type == json::Value::Type::Object) {
        for (const auto& item : want.o) {
            const json::Value* field = got.find(item.first);
            if (field == nullptr || !same(*field, item.second)) {
                return false;
            }
        }
        return true;
    }
    if (want.type == json::Value::Type::Array && got.type == json::Value::Type::Array) {
        if (want.a.size() != got.a.size()) {
            return false;
        }
        for (std::size_t i = 0; i < want.a.size(); ++i) {
            if (!same(got.a[i], want.a[i])) {
                return false;
            }
        }
        return true;
    }
    if (want.type != got.type) {
        return false;
    }
    if (want.type == json::Value::Type::String) {
        return want.s == got.s;
    }
    if (want.type == json::Value::Type::Bool) {
        return want.b == got.b;
    }
    return want.type == json::Value::Type::Null;
}

}  // namespace kiln::test
