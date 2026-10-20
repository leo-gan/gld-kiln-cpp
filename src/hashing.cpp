#include "kiln/schema.hpp"

#include <stdexcept>

namespace kiln {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t mix(std::uint64_t hash, const char* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= static_cast<std::uint8_t>(data[i]);
        hash *= kFnvPrime;
    }
    // Trailing 0x00 is a multiply: xor with zero does not change the hash.
    return hash * kFnvPrime;
}

std::uint64_t mix(std::uint64_t hash, std::string_view text) {
    return mix(hash, text.data(), text.size());
}

const char* scalar_token(Kind kind) {
    switch (kind) {
        case Kind::Bool:
            return "bool";
        case Kind::I8:
            return "i8";
        case Kind::I16:
            return "i16";
        case Kind::I32:
            return "i32";
        case Kind::I64:
            return "i64";
        case Kind::Int:
            return "int";
        case Kind::U8:
            return "u8";
        case Kind::U16:
            return "u16";
        case Kind::U32:
            return "u32";
        case Kind::U64:
            return "u64";
        case Kind::Uint:
            return "uint";
        case Kind::F32:
            return "f32";
        case Kind::F64:
            return "f64";
        case Kind::Str:
            return "str";
        case Kind::Bytes:
            return "bytes";
        case Kind::Tensors:
            return "tensors";
        default:
            return nullptr;
    }
}

std::uint64_t hash_schema(std::uint64_t hash, const Schema& schema) {
    if (const char* token = scalar_token(schema.kind)) {
        return mix(hash, token);
    }
    if (schema.kind == Kind::List) {
        hash = mix(hash, "list<");
        hash = hash_schema(hash, schema.elem.at(0));
        return mix(hash, ">");
    }
    if (schema.kind == Kind::Opt) {
        hash = mix(hash, "opt<");
        hash = hash_schema(hash, schema.elem.at(0));
        return mix(hash, ">");
    }
    if (schema.kind == Kind::Dict) {
        hash = mix(hash, "dict<str,");
        hash = hash_schema(hash, schema.elem.at(0));
        return mix(hash, ">");
    }
    if (schema.kind == Kind::Struct) {
        hash = mix(hash, "struct{");
        for (const auto& field : schema.fields) {
            hash = mix(hash, field.first);
            hash = mix(hash, ":");
            hash = hash_schema(hash, field.second);
            hash = mix(hash, ",");
        }
        return mix(hash, "}");
    }
    throw std::invalid_argument("unknown schema");
}

}  // namespace

std::uint64_t type_hash(const Schema& schema) { return hash_schema(kFnvOffset, schema); }

}  // namespace kiln
