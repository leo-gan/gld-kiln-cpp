#include "codegen_internal.hpp"

#include "kiln/schema.hpp"

#include <string>

namespace kiln {
namespace {

bool signed_kind(Kind kind) {
    return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32 || kind == Kind::I64 || kind == Kind::Int;
}

bool unsigned_kind(Kind kind) {
    return kind == Kind::U8 || kind == Kind::U16 || kind == Kind::U32 || kind == Kind::U64 || kind == Kind::Uint;
}

const char* kind_token(Kind kind) {
    switch (kind) {
        case Kind::Bool: return "Bool";
        case Kind::I8: return "I8";
        case Kind::I16: return "I16";
        case Kind::I32: return "I32";
        case Kind::I64: return "I64";
        case Kind::Int: return "Int";
        case Kind::U8: return "U8";
        case Kind::U16: return "U16";
        case Kind::U32: return "U32";
        case Kind::U64: return "U64";
        case Kind::Uint: return "Uint";
        case Kind::F32: return "F32";
        case Kind::F64: return "F64";
        case Kind::Str: return "Str";
        case Kind::Bytes: return "Bytes";
        default: return "Bool";
    }
}

}  // namespace

std::string cpp_type_name(const Schema& schema) {
    switch (schema.kind) {
        case Kind::Bool: return "bool";
        case Kind::I8: return "std::int8_t";
        case Kind::I16: return "std::int16_t";
        case Kind::I32: return "std::int32_t";
        case Kind::I64: return "std::int64_t";
        case Kind::Int: return "std::int64_t";
        case Kind::U8: return "std::uint8_t";
        case Kind::U16: return "std::uint16_t";
        case Kind::U32: return "std::uint32_t";
        case Kind::U64: return "std::uint64_t";
        case Kind::Uint: return "std::uint64_t";
        case Kind::F32: return "float";
        case Kind::F64: return "double";
        case Kind::Str: return "std::string";
        case Kind::Bytes: return "std::vector<std::uint8_t>";
        case Kind::List:
            if (schema.elem[0].kind == Kind::U8) {
                return "std::vector<std::uint8_t>";
            }
            return "std::vector<" + cpp_type_name(schema.elem[0]) + ">";
        case Kind::Opt:
            return "std::optional<" + cpp_type_name(schema.elem[0]) + ">";
        case Kind::Dict:
            return "std::vector<std::pair<std::string, " + cpp_type_name(schema.elem[0]) + ">>";
        case Kind::Struct:
            return schema.name;
        default:
            return "kiln::Value";
    }
}

std::string schema_expr(const Schema& schema) {
    switch (schema.kind) {
        case Kind::List:
            return "kiln::list_of(" + schema_expr(schema.elem[0]) + ")";
        case Kind::Opt:
            return "kiln::opt_of(" + schema_expr(schema.elem[0]) + ")";
        case Kind::Dict:
            return "kiln::dict_of(" + schema_expr(schema.elem[0]) + ")";
        case Kind::Struct:
            return schema.name + "::schema()";
        default:
            return std::string("kiln::scalar(kiln::Kind::") + kind_token(schema.kind) + ")";
    }
}

std::string member_line(const Schema& schema, const std::string& name) {
    std::string line = "    " + cpp_type_name(schema) + " " + name;
    if (schema.kind == Kind::Bool) {
        line += " = false";
    } else if (signed_kind(schema.kind) || unsigned_kind(schema.kind) || schema.kind == Kind::F32 || schema.kind == Kind::F64) {
        line += " = 0";
    } else if (schema.kind == Kind::Opt) {
        line += " = std::nullopt";
    }
    return line + ";";
}

}  // namespace kiln
