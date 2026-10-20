#include "kiln/schema.hpp"

#include "kiln/bits.hpp"
#include "kiln/error.hpp"

#include <string>

namespace kiln {

int scalar_width(Kind kind) {
    switch (kind) {
        case Kind::Bool:
        case Kind::I8:
        case Kind::U8:
            return 1;
        case Kind::I16:
        case Kind::U16:
            return 2;
        case Kind::I32:
        case Kind::U32:
        case Kind::F32:
            return 4;
        case Kind::I64:
        case Kind::U64:
        case Kind::Int:
        case Kind::Uint:
        case Kind::F64:
            return 8;
        default:
            return -1;
    }
}

namespace {

Kind kind_from_token(std::string_view text) {
    static const std::pair<std::string_view, Kind> kTable[] = {
        {"bool", Kind::Bool}, {"i8", Kind::I8},     {"i16", Kind::I16}, {"i32", Kind::I32},
        {"i64", Kind::I64},   {"int", Kind::Int},   {"u8", Kind::U8},   {"u16", Kind::U16},
        {"u32", Kind::U32},   {"u64", Kind::U64},   {"uint", Kind::Uint}, {"f32", Kind::F32},
        {"f64", Kind::F64},   {"str", Kind::Str},   {"bytes", Kind::Bytes}, {"tensors", Kind::Tensors},
    };
    for (const auto& item : kTable) {
        if (item.first == text) {
            return item.second;
        }
    }
    throw Error("SCHEMA");
}

std::size_t matching_close(std::string_view text, std::size_t open, char end) {
    int depth = 1;
    for (std::size_t i = open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '<' || c == '{') {
            ++depth;
        } else if (c == '>' || c == '}') {
            --depth;
            if (depth == 0) {
                if (c != end) {
                    throw Error("SCHEMA");
                }
                return i;
            }
        }
    }
    throw Error("SCHEMA");
}

Schema parse_type(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    if (text.size() >= 7 && text.substr(0, 7) == "struct{") {
        if (text.back() != '}') {
            throw Error("SCHEMA");
        }
        const std::string_view body = text.substr(7, text.size() - 8);
        std::vector<std::pair<std::string, Schema>> fields;
        std::size_t i = 0;
        while (i < body.size()) {
            if (body[i] == ',') {
                ++i;
                continue;
            }
            const std::size_t colon = body.find(':', i);
            if (colon == std::string_view::npos) {
                throw Error("SCHEMA");
            }
            std::string name(body.substr(i, colon - i));
            std::size_t end = colon + 1;
            int depth = 0;
            while (end < body.size()) {
                const char c = body[end];
                if (c == '<' || c == '{') {
                    ++depth;
                } else if (c == '>' || c == '}') {
                    --depth;
                } else if (c == ',' && depth == 0) {
                    break;
                }
                ++end;
            }
            fields.emplace_back(std::move(name), parse_type(body.substr(colon + 1, end - colon - 1)));
            i = end;
        }
        return struct_of(std::move(fields));
    }
    if (text.size() >= 5 && text.substr(0, 5) == "list<") {
        const std::size_t end = matching_close(text, 4, '>');
        if (end + 1 != text.size()) {
            throw Error("SCHEMA");
        }
        return list_of(parse_type(text.substr(5, end - 5)));
    }
    if (text.size() >= 4 && text.substr(0, 4) == "opt<") {
        const std::size_t end = matching_close(text, 3, '>');
        if (end + 1 != text.size()) {
            throw Error("SCHEMA");
        }
        return opt_of(parse_type(text.substr(4, end - 4)));
    }
    if (text.size() >= 9 && text.substr(0, 9) == "dict<str,") {
        const std::size_t end = matching_close(text, 4, '>');
        if (end + 1 != text.size()) {
            throw Error("SCHEMA");
        }
        return dict_of(parse_type(text.substr(9, end - 9)));
    }
    return scalar(kind_from_token(text));
}

}  // namespace

int min_size(const Schema& schema) {
    const int width = scalar_width(schema.kind);
    if (width >= 0) {
        return width;
    }
    if (schema.kind == Kind::Str || schema.kind == Kind::Bytes) {
        return 4;
    }
    if (schema.kind == Kind::Tensors) {
        return static_cast<int>(kHeader);
    }
    if (schema.kind == Kind::Opt) {
        return 1;
    }
    if (schema.kind == Kind::List || schema.kind == Kind::Dict) {
        return 4;
    }
    if (schema.kind == Kind::Struct) {
        int total = 0;
        for (const auto& field : schema.fields) {
            total += min_size(field.second);
        }
        return total;
    }
    throw Error("TYPE");
}

Schema parse_type_text(std::string_view text) { return parse_type(text); }

}  // namespace kiln
