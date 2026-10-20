#include "kiln/schema.hpp"

#include "kiln/error.hpp"
#include "kiln/json.hpp"

#include <map>
#include <set>
#include <string>

namespace kiln {
namespace {

Kind integer_kind(const json::Value& node) {
    const json::Value* format = node.find("format");
    if (format == nullptr || !format->is_string()) {
        return Kind::I64;
    }
    static const std::pair<std::string_view, Kind> kTable[] = {
        {"int8", Kind::I8},   {"int16", Kind::I16}, {"int32", Kind::I32}, {"int64", Kind::I64},
        {"uint8", Kind::U8}, {"uint16", Kind::U16}, {"uint32", Kind::U32}, {"uint64", Kind::U64},
    };
    for (const auto& item : kTable) {
        if (item.first == format->s) {
            return item.second;
        }
    }
    return Kind::I64;
}

bool required_has(const json::Value* required, const std::string& name) {
    if (required == nullptr || !required->is_array()) {
        return true;
    }
    for (const json::Value& item : required->a) {
        if (item.is_string() && item.s == name) {
            return true;
        }
    }
    return false;
}

struct Converter {
    const json::Value* defs = nullptr;
    std::map<std::string, Schema> cache;
    std::set<std::string> building;

    Schema convert(const json::Value& node, const std::string& label) {
        if (!node.is_object()) {
            throw Error("SCHEMA");
        }
        if (const json::Value* ref = node.find("$ref")) {
            if (!ref->is_string() || ref->s.rfind("#/$defs/", 0) != 0) {
                throw Error("SCHEMA");
            }
            return resolve(ref->s.substr(std::string("#/$defs/").size()));
        }
        const json::Value* kind = node.find("type");
        if (kind == nullptr || !kind->is_string()) {
            throw Error("SCHEMA");
        }
        if (kind->s == "boolean") {
            return scalar(Kind::Bool);
        }
        if (kind->s == "string") {
            const json::Value* format = node.find("format");
            if (format != nullptr && format->is_string() && format->s == "binary") {
                return scalar(Kind::Bytes);
            }
            return scalar(Kind::Str);
        }
        if (kind->s == "integer") {
            return scalar(integer_kind(node));
        }
        if (kind->s == "number") {
            const json::Value* format = node.find("format");
            if (format != nullptr && format->is_string() && format->s == "float") {
                return scalar(Kind::F32);
            }
            return scalar(Kind::F64);
        }
        if (kind->s == "array") {
            const json::Value* items = node.find("items");
            if (items == nullptr || !items->is_object()) {
                throw Error("SCHEMA");
            }
            return list_of(convert(*items, label + "Item"));
        }
        if (kind->s == "object") {
            const json::Value* props = node.find("properties");
            const json::Value* extra = node.find("additionalProperties");
            const bool no_props = props == nullptr || !props->is_object() || props->o.empty();
            if (extra != nullptr && extra->is_object() && no_props) {
                return dict_of(convert(*extra, label + "Value"));
            }
            return object_schema(node, label);
        }
        throw Error("SCHEMA");
    }

    Schema object_schema(const json::Value& node, const std::string& label) {
        const json::Value* props = node.find("properties");
        const json::Value* required = node.find("required");
        std::vector<std::pair<std::string, Schema>> fields;
        if (props != nullptr && props->is_object()) {
            for (const auto& item : props->o) {
                Schema field = convert(item.second, label + "_" + item.first);
                if (!required_has(required, item.first)) {
                    field = opt_of(std::move(field));
                }
                fields.emplace_back(item.first, std::move(field));
            }
        }
        return struct_of(std::move(fields), label);
    }

    Schema resolve(const std::string& name) {
        const auto found = cache.find(name);
        if (found != cache.end()) {
            return found->second;
        }
        if (building.count(name) != 0 || defs == nullptr || !defs->is_object()) {
            throw Error("SCHEMA");
        }
        const json::Value* node = defs->find(name);
        if (node == nullptr) {
            throw Error("SCHEMA");
        }
        building.insert(name);
        Schema built = convert(*node, name);
        building.erase(name);
        cache.emplace(name, built);
        return built;
    }
};

}  // namespace

Schema schema_from_case(const json::Value& schema) {
    if (schema.is_string()) {
        if (schema.s == "tensor-v2") {
            return scalar(Kind::Tensors);
        }
        if (schema.s == "frame") {
            return struct_of({{"id", scalar(Kind::I64)}, {"batch", scalar(Kind::Tensors)}});
        }
        throw Error("SCHEMA");
    }
    if (!schema.is_object()) {
        throw Error("SCHEMA");
    }
    const json::Value* type = schema.find("type");
    if (type == nullptr || !type->is_string()) {
        throw Error("SCHEMA");
    }
    if (type->s == "list") {
        const json::Value* element = schema.find("element");
        if (element == nullptr || !element->is_string()) {
            throw Error("SCHEMA");
        }
        return list_of(parse_type_text(element->s));
    }
    if (type->s == "struct") {
        const json::Value* fields = schema.find("fields");
        if (fields == nullptr || !fields->is_array()) {
            throw Error("SCHEMA");
        }
        std::vector<std::pair<std::string, Schema>> out;
        for (const json::Value& item : fields->a) {
            if (!item.is_string()) {
                throw Error("SCHEMA");
            }
            const std::size_t colon = item.s.find(':');
            if (colon == std::string::npos) {
                throw Error("SCHEMA");
            }
            out.emplace_back(item.s.substr(0, colon), parse_type_text(item.s.substr(colon + 1)));
        }
        return struct_of(std::move(out));
    }
    throw Error("SCHEMA");
}

NamedSchema schema_from_json(std::string_view text) {
    const json::Value doc = json::parse(text);
    if (!doc.is_object()) {
        throw Error("SCHEMA");
    }
    Converter converter;
    if (const json::Value* defs = doc.find("$defs")) {
        if (!defs->is_object()) {
            throw Error("SCHEMA");
        }
        converter.defs = defs;
    }
    std::string title = "Root";
    if (const json::Value* name = doc.find("title")) {
        if (name->is_string() && !name->s.empty()) {
            title = name->s;
        }
    }
    return NamedSchema{title, converter.convert(doc, title)};
}

}  // namespace kiln
