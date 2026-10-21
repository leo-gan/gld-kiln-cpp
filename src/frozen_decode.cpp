#include "kiln/frozen.hpp"

#include "pod_box.hpp"

#include "kiln/error.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace kiln {
namespace {

struct Cursor {
    const std::uint8_t* p = nullptr;
    std::size_t n = 0;
    std::size_t i = 0;
    std::uint64_t cap = kCap;
    bool trusted = false;

    void need(std::size_t k) const {
        if (i > n || k > n - i) {
            throw Error("EOF");
        }
    }
};

const Schema& inner_of(const Schema& schema) {
    if (schema.elem.empty()) {
        throw Error("TYPE");
    }
    return schema.elem[0];
}

Value read_value(Cursor& cursor, const Schema& schema, int depth);
void read_struct(Cursor& cursor, const Schema& schema, int depth, Value& out);

void read_pod_elems(Cursor& cursor, Kind kind, Value& slot) {
    const PodCode code = pod_code(kind);
    if (code == PodCode::None) {
        throw Error("TYPE");
    }
    cursor.need(4);
    const std::uint32_t count = load_u32(cursor.p + cursor.i);
    const std::uint64_t unread = cursor.n - (cursor.i + 4);
    const int width = pod_width(code);
    fit_count(count, static_cast<std::uint64_t>(width), unread, cursor.cap);
    cursor.i += 4;
    const std::uint64_t bytes = mul_u64(count, static_cast<std::uint64_t>(width));
    if (bytes > std::numeric_limits<std::size_t>::max()) {
        throw Error("LIMIT");
    }
    cursor.need(static_cast<std::size_t>(bytes));
    const std::uint8_t* src = bytes == 0 ? nullptr : cursor.p + cursor.i;
    PodOps::fill(slot, code, src, count);
    cursor.i += static_cast<std::size_t>(bytes);
}

Value read_inline(Cursor& cursor, Kind kind, std::uint64_t cap, bool trusted) {
    const int width = scalar_width(kind);
    if (kind == Kind::Bool) {
        cursor.need(1);
        const std::uint8_t byte = cursor.p[cursor.i++];
        if (byte > 1) {
            throw Error("TYPE");
        }
        return Value::boolean(byte != 0);
    }
    if (width > 0 && kind != Kind::F32 && kind != Kind::F64) {
        cursor.need(static_cast<std::size_t>(width));
        const std::uint8_t* raw = cursor.p + cursor.i;
        cursor.i += static_cast<std::size_t>(width);
        if (kind == Kind::I8) {
            return Value::i64(static_cast<std::int8_t>(raw[0]));
        }
        if (kind == Kind::U8) {
            return Value::u64(raw[0]);
        }
        if (kind == Kind::I16) {
            return Value::i64(static_cast<std::int16_t>(load_u16(raw)));
        }
        if (kind == Kind::U16) {
            return Value::u64(load_u16(raw));
        }
        if (kind == Kind::I32) {
            return Value::i64(static_cast<std::int32_t>(load_u32(raw)));
        }
        if (kind == Kind::U32) {
            return Value::u64(load_u32(raw));
        }
        if (kind == Kind::I64 || kind == Kind::Int) {
            return Value::i64(static_cast<std::int64_t>(load_u64(raw)));
        }
        return Value::u64(load_u64(raw));
    }
    if (kind == Kind::F32) {
        cursor.need(4);
        const std::uint32_t bits = load_u32(cursor.p + cursor.i);
        cursor.i += 4;
        return Value::f32_bits(bits);
    }
    if (kind == Kind::F64) {
        cursor.need(8);
        const std::uint64_t bits = load_u64(cursor.p + cursor.i);
        cursor.i += 8;
        return Value::f64_bits(bits);
    }
    cursor.need(4);
    const std::uint32_t length = load_u32(cursor.p + cursor.i);
    cursor.i += 4;
    if (static_cast<std::uint64_t>(length) > cap) {
        throw Error("LIMIT");
    }
    cursor.need(length);
    const std::uint8_t* raw = cursor.p + cursor.i;
    cursor.i += length;
    if (kind == Kind::Bytes) {
        return Value::bytes(std::vector<std::uint8_t>(raw, raw + length));
    }
    if (kind != Kind::Str) {
        throw Error("TYPE");
    }
    if (!trusted && !utf8_ok(raw, length)) {
        throw Error("UTF8");
    }
    return Value::str(std::string(reinterpret_cast<const char*>(raw), length));
}

Value read_list(Cursor& cursor, const Schema& elem, int depth) {
    (void)depth;
    if (pod_code(elem.kind) != PodCode::None) {
        Value slot;
        read_pod_elems(cursor, elem.kind, slot);
        return slot;
    }
    cursor.need(4);
    const std::uint32_t count = load_u32(cursor.p + cursor.i);
    const std::uint64_t unread = cursor.n - (cursor.i + 4);
    fit_count(count, static_cast<std::uint64_t>(min_size(elem)), unread, cursor.cap);
    cursor.i += 4;
    if (elem.kind == Kind::U8) {
        cursor.need(count);
        const std::uint8_t* raw = cursor.p + cursor.i;
        cursor.i += count;
        return Value::bytes(std::vector<std::uint8_t>(raw, raw + count));
    }
    const int width = scalar_width(elem.kind);
    Value::List out;
    out.reserve(count);
    if (elem.kind == Kind::I8) {
        cursor.need(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            out.push_back(Value::i64(static_cast<std::int8_t>(cursor.p[cursor.i++])));
        }
        return Value::list(std::move(out));
    }
    if (width > 0 && elem.kind != Kind::Bool) {
        cursor.need(static_cast<std::size_t>(count) * static_cast<std::size_t>(width));
        for (std::uint32_t index = 0; index < count; ++index) {
            out.push_back(read_inline(cursor, elem.kind, cursor.cap, cursor.trusted));
        }
        return Value::list(std::move(out));
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        out.push_back(read_value(cursor, elem, depth + 1));
    }
    return Value::list(std::move(out));
}

Value read_value(Cursor& cursor, const Schema& schema, int depth) {
    if (depth > kMaxDepth) {
        throw Error("LIMIT");
    }
    const Kind kind = schema.kind;
    if (kind == Kind::List) {
        return read_list(cursor, inner_of(schema), depth);
    }
    if (kind == Kind::Opt) {
        cursor.need(1);
        const std::uint8_t tag = cursor.p[cursor.i++];
        if (tag == 0) {
            return Value::null();
        }
        if (tag != 1) {
            throw Error("TYPE");
        }
        return read_value(cursor, inner_of(schema), depth + 1);
    }
    if (kind == Kind::Dict) {
        cursor.need(4);
        const std::uint32_t count = load_u32(cursor.p + cursor.i);
        fit_count(count, 4, cursor.n - (cursor.i + 4), cursor.cap);
        cursor.i += 4;
        Value out;
        Value::Dict& fields = out.reset_dict(count);
        std::string prev;
        bool have_prev = false;
        const Schema key = scalar(Kind::Str);
        for (std::uint32_t index = 0; index < count; ++index) {
            Value key_value = read_value(cursor, key, depth + 1);
            std::string name = key_value.as_str();
            if (have_prev && name < prev) {
                throw Error("TYPE");
            }
            prev = name;
            have_prev = true;
            Value item = read_value(cursor, inner_of(schema), depth + 1);
            bool replaced = false;
            for (auto& field : fields) {
                if (field.first == name) {
                    field.second = std::move(item);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                fields.emplace_back(std::move(name), std::move(item));
            }
        }
        return out;
    }
    if (kind == Kind::Struct) {
        Value out;
        read_struct(cursor, schema, depth, out);
        return out;
    }
    if (kind == Kind::Tensors) {
        const auto rest = std::span<const std::uint8_t>(cursor.p + cursor.i, cursor.n - cursor.i);
        TensorDirectory directory = decode_tensors(rest, ReadOpts{cursor.cap, false, cursor.trusted});
        cursor.i += static_cast<std::size_t>(directory.block_length);
        return Value::tensors(std::move(directory));
    }
    return read_inline(cursor, kind, cursor.cap, cursor.trusted);
}

void read_struct(Cursor& cursor, const Schema& schema, int depth, Value& out) {
    Value::Dict& fields = out.scratch_dict();
    const std::size_t count = schema.fields.size();
    bool reuse = fields.size() == count;
    for (std::size_t index = 0; reuse && index < count; ++index) {
        reuse = fields[index].first == schema.fields[index].first;
    }
    if (!reuse) {
        fields.clear();
        if (fields.capacity() < count) {
            fields.reserve(count);
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const Schema& field = schema.fields[index].second;
        if (field.kind == Kind::List && !field.elem.empty() && pod_code(field.elem[0].kind) != PodCode::None) {
            if (!reuse) {
                fields.emplace_back(schema.fields[index].first, Value());
            }
            read_pod_elems(cursor, field.elem[0].kind, fields[index].second);
            continue;
        }
        Value item = scalar_width(field.kind) > 0 ? read_inline(cursor, field.kind, cursor.cap, cursor.trusted)
                                                   : read_value(cursor, field, depth + 1);
        if (reuse) {
            fields[index].second = std::move(item);
        } else {
            fields.emplace_back(schema.fields[index].first, std::move(item));
        }
    }
}

void read_into(Cursor& cursor, const Schema& schema, int depth, Value& slot) {
    if (depth > kMaxDepth) {
        throw Error("LIMIT");
    }
    if (schema.kind == Kind::List) {
        if (!schema.elem.empty() && pod_code(schema.elem[0].kind) != PodCode::None) {
            read_pod_elems(cursor, schema.elem[0].kind, slot);
            return;
        }
        slot = read_list(cursor, inner_of(schema), depth);
        return;
    }
    if (schema.kind == Kind::Struct) {
        read_struct(cursor, schema, depth, slot);
        return;
    }
    slot = read_value(cursor, schema, depth);
}

}  // namespace

Value decode(const Schema& schema, std::span<const std::uint8_t> buf, const ReadOpts& opts) {
    Cursor cursor{buf.data(), buf.size(), 0, opts.cap, opts.trusted};
    Value value = read_value(cursor, schema, 0);
    if (opts.standalone && cursor.i != cursor.n) {
        throw Error("TRAILING");
    }
    return value;
}

void decode_into(const Schema& schema, std::span<const std::uint8_t> buf, Value& out, const ReadOpts& opts) {
    Cursor cursor{buf.data(), buf.size(), 0, opts.cap, opts.trusted};
    read_into(cursor, schema, 0, out);
    if (opts.standalone && cursor.i != cursor.n) {
        throw Error("TRAILING");
    }
}

Value decode_framed(const Schema& schema, std::span<const std::uint8_t> buf, const ReadOpts& opts) {
    if (buf.size() < 16) {
        throw Error("EOF");
    }
    if (std::memcmp(buf.data(), "KLN0", 4) != 0 || buf[4] != 0 || buf[5] != 0 || buf[6] != 0 || buf[7] != 0) {
        throw Error("FRAME");
    }
    if (load_u64(buf.data() + 8) != type_hash(schema)) {
        throw Error("SCHEMA");
    }
    ReadOpts body = opts;
    body.standalone = true;
    return decode(schema, buf.subspan(16), body);
}

}  // namespace kiln
