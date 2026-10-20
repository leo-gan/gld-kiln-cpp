#include "kiln/frozen.hpp"

#include "kiln/error.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace kiln {
namespace {

const Schema& inner_of(const Schema& schema) {
    if (schema.elem.empty()) {
        throw Error("TYPE");
    }
    return schema.elem[0];
}

bool container(Kind kind) {
    return kind == Kind::List || kind == Kind::Opt || kind == Kind::Dict || kind == Kind::Struct || kind == Kind::Tensors;
}

}  // namespace

void Writer::write_value(const Schema& schema, const Value& value, std::uint64_t cap, int depth) {
    if (depth > kMaxDepth) {
        throw Error("LIMIT");
    }
    const Kind kind = schema.kind;
    if (kind == Kind::Opt) {
        alloc(1)[0] = value.is_null() ? 0 : 1;
        if (!value.is_null()) {
            write_value(inner_of(schema), value, cap, depth + 1);
        }
        return;
    }
    if (kind == Kind::List) {
        write_list(inner_of(schema), value, cap, depth);
        return;
    }
    if (kind == Kind::Dict) {
        const auto& items = value.as_dict();
        std::vector<const std::pair<std::string, Value>*> order;
        order.reserve(items.size());
        for (const auto& item : items) {
            order.push_back(&item);
        }
        std::stable_sort(order.begin(), order.end(), [](const auto* left, const auto* right) { return left->first < right->first; });
        fit_count(order.size(), 4, UINT64_MAX, cap);
        store_u32(alloc(4), static_cast<std::uint32_t>(order.size()));
        for (const auto* item : order) {
            write_value(scalar(Kind::Str), Value::str(item->first), cap, depth + 1);
            write_value(inner_of(schema), item->second, cap, depth + 1);
        }
        return;
    }
    if (kind == Kind::Struct) {
        if (value.tag() != Value::Tag::Dict) {
            throw Error("TYPE");
        }
        for (const auto& field : schema.fields) {
            const Value* item = value.find(field.first);
            if (item == nullptr) {
                throw Error("TYPE");
            }
            const Kind child = field.second.kind;
            if (child == Kind::Str || child == Kind::Bytes || container(child)) {
                if (child == Kind::Str || child == Kind::Bytes) {
                    write_leaf(child, *item, cap);
                } else {
                    write_value(field.second, *item, cap, depth + 1);
                }
            } else {
                write_leaf(child, *item, cap);
            }
        }
        return;
    }
    if (kind == Kind::Tensors) {
        const std::vector<std::uint8_t> raw = encode_tensors(value.as_tensors().tensors);
        if (!raw.empty()) {
            std::memcpy(alloc(raw.size()), raw.data(), raw.size());
        }
        return;
    }
    write_leaf(kind, value, cap);
}

std::span<const std::uint8_t> Writer::encode(const Schema& schema, const Value& value, std::uint64_t cap) {
    end_ = 0;
    write_value(schema, value, cap, 0);
    return bytes();
}

std::span<const std::uint8_t> Writer::encode_framed(const Schema& schema, const Value& value, std::uint64_t cap) {
    end_ = 0;
    std::uint8_t* header = alloc(16);
    std::memcpy(header, "KLN0", 4);
    header[4] = header[5] = header[6] = header[7] = 0;
    store_u64(header + 8, type_hash(schema));
    write_value(schema, value, cap, 0);
    return bytes();
}

std::vector<std::uint8_t> encode(const Schema& schema, const Value& value, std::uint64_t cap) {
    Writer writer;
    const auto raw = writer.encode(schema, value, cap);
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

std::vector<std::uint8_t> encode_framed(const Schema& schema, const Value& value, std::uint64_t cap) {
    Writer writer;
    const auto raw = writer.encode_framed(schema, value, cap);
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

}  // namespace kiln
