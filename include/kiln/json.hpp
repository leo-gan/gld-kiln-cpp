#pragma once

#include "kiln/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    bool is_int = false;
    bool is_uint = false;
    std::int64_t i = 0;
    std::uint64_t u = 0;
    double d = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;

    bool is_null() const { return type == Type::Null; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }
    bool is_number() const { return type == Type::Number; }

    const Value* find(std::string_view key) const {
        for (const auto& item : o) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }
};

Value parse(std::string_view text);

}  // namespace kiln::json
