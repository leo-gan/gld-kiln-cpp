#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln {

enum class Kind : std::uint8_t {
    Bool,
    I8,
    I16,
    I32,
    I64,
    Int,
    U8,
    U16,
    U32,
    U64,
    Uint,
    F32,
    F64,
    Str,
    Bytes,
    Tensors,
    List,
    Opt,
    Dict,
    Struct
};

struct Schema {
    Kind kind = Kind::Bool;
    std::string name;
    std::vector<Schema> elem;
    std::vector<std::pair<std::string, Schema>> fields;
};

struct NamedSchema {
    std::string title;
    Schema schema;
};

inline Schema scalar(Kind kind) {
    Schema schema;
    schema.kind = kind;
    return schema;
}

inline Schema list_of(Schema inner) {
    Schema schema;
    schema.kind = Kind::List;
    schema.elem.push_back(std::move(inner));
    return schema;
}

inline Schema opt_of(Schema inner) {
    Schema schema;
    schema.kind = Kind::Opt;
    schema.elem.push_back(std::move(inner));
    return schema;
}

inline Schema dict_of(Schema inner) {
    Schema schema;
    schema.kind = Kind::Dict;
    schema.elem.push_back(std::move(inner));
    return schema;
}

inline Schema struct_of(std::vector<std::pair<std::string, Schema>> fields, std::string name = {}) {
    Schema schema;
    schema.kind = Kind::Struct;
    schema.name = std::move(name);
    schema.fields = std::move(fields);
    return schema;
}

int scalar_width(Kind kind);
int min_size(const Schema& schema);
std::uint64_t type_hash(const Schema& schema);

Schema parse_type_text(std::string_view text);

namespace json {
struct Value;
}

Schema schema_from_case(const json::Value& schema);
NamedSchema schema_from_json(std::string_view text);

}  // namespace kiln
