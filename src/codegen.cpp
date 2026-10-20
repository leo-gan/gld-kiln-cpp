#include "kiln/codegen.hpp"

#include "codegen_internal.hpp"

#include "kiln/error.hpp"

#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln {
namespace {

std::string escape_cpp(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

void collect(const Schema& schema, std::vector<const Schema*>& out, std::set<std::string>& seen) {
    if (schema.kind == Kind::List || schema.kind == Kind::Opt || schema.kind == Kind::Dict) {
        if (!schema.elem.empty()) {
            collect(schema.elem[0], out, seen);
        }
        return;
    }
    if (schema.kind != Kind::Struct) {
        return;
    }
    for (const auto& field : schema.fields) {
        collect(field.second, out, seen);
    }
    if (seen.insert(schema.name).second) {
        out.push_back(&schema);
    }
}

bool signed_kind(Kind kind) {
    return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32 || kind == Kind::I64 || kind == Kind::Int;
}

bool unsigned_kind(Kind kind) {
    return kind == Kind::U8 || kind == Kind::U16 || kind == Kind::U32 || kind == Kind::U64 || kind == Kind::Uint;
}

void emit_store(const Schema& schema, const std::string& expr, const std::string& dest, std::vector<std::string>& lines, int& id) {
    if (schema.kind == Kind::Bool) {
        lines.push_back(dest + " = kiln::Value::boolean(" + expr + ");");
    } else if (signed_kind(schema.kind)) {
        lines.push_back(dest + " = kiln::Value::i64(static_cast<std::int64_t>(" + expr + "));");
    } else if (unsigned_kind(schema.kind)) {
        lines.push_back(dest + " = kiln::Value::u64(static_cast<std::uint64_t>(" + expr + "));");
    } else if (schema.kind == Kind::F32) {
        lines.push_back(dest + " = kiln::Value::f32(" + expr + ");");
    } else if (schema.kind == Kind::F64) {
        lines.push_back(dest + " = kiln::Value::f64(" + expr + ");");
    } else if (schema.kind == Kind::Str) {
        lines.push_back(dest + " = kiln::Value::str(" + expr + ");");
    } else if (schema.kind == Kind::Bytes || (schema.kind == Kind::List && schema.elem[0].kind == Kind::U8)) {
        lines.push_back(dest + " = kiln::Value::bytes(" + expr + ");");
    } else if (schema.kind == Kind::Struct) {
        lines.push_back(dest + " = " + expr + ".to_value();");
    } else if (schema.kind == Kind::Opt) {
        lines.push_back("if (" + expr + ".has_value()) {");
        emit_store(schema.elem[0], "(*" + expr + ")", dest, lines, id);
        lines.push_back("} else {");
        lines.push_back(dest + " = kiln::Value::null();");
        lines.push_back("}");
    } else if (schema.kind == Kind::List) {
        const std::string list = "list" + std::to_string(id++);
        const std::string item = "item" + std::to_string(id++);
        const std::string inner = "iv" + std::to_string(id++);
        lines.push_back("kiln::Value::List " + list + ";");
        lines.push_back(list + ".reserve(" + expr + ".size());");
        lines.push_back("for (const auto& " + item + " : " + expr + ") {");
        lines.push_back("kiln::Value " + inner + ";");
        emit_store(schema.elem[0], item, inner, lines, id);
        lines.push_back(list + ".push_back(std::move(" + inner + "));");
        lines.push_back("}");
        lines.push_back(dest + " = kiln::Value::list(std::move(" + list + "));");
    } else if (schema.kind == Kind::Dict) {
        const std::string dict = "dict" + std::to_string(id++);
        const std::string pair = "pair" + std::to_string(id++);
        const std::string inner = "dv" + std::to_string(id++);
        lines.push_back("kiln::Value::Dict " + dict + ";");
        lines.push_back(dict + ".reserve(" + expr + ".size());");
        lines.push_back("for (const auto& " + pair + " : " + expr + ") {");
        lines.push_back("kiln::Value " + inner + ";");
        emit_store(schema.elem[0], pair + ".second", inner, lines, id);
        lines.push_back(dict + ".emplace_back(" + pair + ".first, std::move(" + inner + "));");
        lines.push_back("}");
        lines.push_back(dest + " = kiln::Value::dict(std::move(" + dict + "));");
    } else {
        throw Error("SCHEMA");
    }
}

void emit_load(const Schema& schema, const std::string& src, const std::string& dest, std::vector<std::string>& lines, int& id) {
    if (schema.kind == Kind::Bool) {
        lines.push_back(dest + " = " + src + ".as_bool();");
    } else if (signed_kind(schema.kind) || unsigned_kind(schema.kind)) {
        const char* acc = unsigned_kind(schema.kind) ? "as_u64()" : "as_i64()";
        lines.push_back(dest + " = static_cast<" + cpp_type_name(schema) + ">(" + src + "." + acc + ");");
    } else if (schema.kind == Kind::F32) {
        lines.push_back(dest + " = " + src + ".as_f32();");
    } else if (schema.kind == Kind::F64) {
        lines.push_back(dest + " = " + src + ".as_f64();");
    } else if (schema.kind == Kind::Str) {
        lines.push_back(dest + " = " + src + ".as_str();");
    } else if (schema.kind == Kind::Bytes || (schema.kind == Kind::List && schema.elem[0].kind == Kind::U8)) {
        lines.push_back(dest + " = " + src + ".as_bytes();");
    } else if (schema.kind == Kind::Struct) {
        lines.push_back(dest + " = " + schema.name + "::from_value(" + src + ");");
    } else if (schema.kind == Kind::Opt) {
        const std::string tmp = "opt" + std::to_string(id++);
        lines.push_back("if (" + src + ".is_null()) {");
        lines.push_back(dest + " = std::nullopt;");
        lines.push_back("} else {");
        lines.push_back(cpp_type_name(schema.elem[0]) + " " + tmp + "{};");
        emit_load(schema.elem[0], src, tmp, lines, id);
        lines.push_back(dest + " = std::move(" + tmp + ");");
        lines.push_back("}");
    } else if (schema.kind == Kind::List) {
        const std::string item = "el" + std::to_string(id++);
        const std::string one = "one" + std::to_string(id++);
        lines.push_back(dest + ".clear();");
        lines.push_back(dest + ".reserve(" + src + ".as_list().size());");
        lines.push_back("for (const kiln::Value& " + item + " : " + src + ".as_list()) {");
        lines.push_back(cpp_type_name(schema.elem[0]) + " " + one + "{};");
        emit_load(schema.elem[0], item, one, lines, id);
        lines.push_back(dest + ".push_back(std::move(" + one + "));");
        lines.push_back("}");
    } else if (schema.kind == Kind::Dict) {
        const std::string pair = "dp" + std::to_string(id++);
        const std::string one = "dv" + std::to_string(id++);
        lines.push_back(dest + ".clear();");
        lines.push_back(dest + ".reserve(" + src + ".as_dict().size());");
        lines.push_back("for (const auto& " + pair + " : " + src + ".as_dict()) {");
        lines.push_back(cpp_type_name(schema.elem[0]) + " " + one + "{};");
        emit_load(schema.elem[0], pair + ".second", one, lines, id);
        lines.push_back(dest + ".emplace_back(" + pair + ".first, std::move(" + one + "));");
        lines.push_back("}");
    } else {
        throw Error("SCHEMA");
    }
}

void append_block(std::ostringstream& out, const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        out << "        " << line << "\n";
    }
}

}  // namespace

std::string emit_cpp(std::string_view json_text) {
    const NamedSchema named = schema_from_json(json_text);
    if (named.schema.kind != Kind::Struct) {
        throw std::runtime_error("root must be an object");
    }
    Schema root = named.schema;
    root.name = named.title.empty() ? "Root" : named.title;
    std::vector<const Schema*> structs;
    std::set<std::string> seen;
    collect(root, structs, seen);
    std::ostringstream out;
    out << "// Generated by gld-kilngen-cpp. Do not edit.\n#pragma once\n\n";
    out << "#include <cstdint>\n#include <optional>\n#include <span>\n#include <string>\n";
    out << "#include <utility>\n#include <vector>\n\n#include \"kiln/kiln.hpp\"\n\nnamespace kiln {\n\n";
    for (const Schema* schema : structs) {
        int id = 0;
        out << "struct " << schema->name << " {\n";
        for (const auto& field : schema->fields) {
            out << member_line(field.second, field.first) << "\n";
        }
        out << "\n    std::vector<std::uint8_t> encode() const {\n        return kiln::encode(schema(), to_value());\n    }\n\n";
        out << "    static " << schema->name << " decode(std::span<const std::uint8_t> buf) {\n";
        out << "        return from_value(kiln::decode(schema(), buf));\n    }\n\n";
        out << "    static std::size_t encoded_size(const " << schema->name << "& value) {\n";
        out << "        return kiln::encode(schema(), value.to_value()).size();\n    }\n\n";
        out << "    kiln::Value to_value() const {\n        kiln::Value::Dict fields;\n        kiln::Value slot;\n";
        for (const auto& field : schema->fields) {
            std::vector<std::string> lines;
            emit_store(field.second, field.first, "slot", lines, id);
            append_block(out, lines);
            out << "        fields.emplace_back(\"" << escape_cpp(field.first) << "\", std::move(slot));\n";
        }
        out << "        return kiln::Value::dict(std::move(fields));\n    }\n\n";
        out << "    static " << schema->name << " from_value(const kiln::Value& value) {\n        " << schema->name << " out;\n";
        for (const auto& field : schema->fields) {
            std::vector<std::string> lines;
            const std::string src = "(*value.find(\"" + escape_cpp(field.first) + "\"))";
            emit_load(field.second, src, "out." + field.first, lines, id);
            append_block(out, lines);
        }
        out << "        return out;\n    }\n\n";
        out << "    static const kiln::Schema& schema() {\n        static const kiln::Schema stored = make_schema();\n        return stored;\n    }\n\n";
        out << "private:\n    static kiln::Schema make_schema() {\n        kiln::Schema schema;\n";
        out << "        schema.kind = kiln::Kind::Struct;\n        schema.name = \"" << escape_cpp(schema->name) << "\";\n";
        for (const auto& field : schema->fields) {
            const std::string expr = field.second.kind == Kind::Struct && field.second.name == schema->name
                                         ? "schema"
                                         : schema_expr(field.second);
            out << "        schema.fields.emplace_back(\"" << escape_cpp(field.first) << "\", " << expr << ");\n";
        }
        out << "        return schema;\n    }\n};\n\n";
    }
    out << "}  // namespace kiln\n";
    return out.str();
}

}  // namespace kiln
