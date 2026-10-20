#include "kiln/frozen.hpp"

#include "kiln/error.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace kiln {
namespace {

bool is_signed(Kind kind) {
    return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32 || kind == Kind::I64 || kind == Kind::Int;
}

void put_unsigned(std::uint8_t* dest, std::uint64_t value, int width);

void put_signed(std::uint8_t* dest, std::int64_t value, int width) {
    const std::int64_t min = width == 1 ? -128 : width == 2 ? -32768 : width == 4 ? std::numeric_limits<std::int32_t>::min() : INT64_MIN;
    const std::int64_t max = width == 1 ? 127 : width == 2 ? 32767 : width == 4 ? std::numeric_limits<std::int32_t>::max() : INT64_MAX;
    if (value < min || value > max) {
        throw Error("TYPE");
    }
    if (width == 1) {
        dest[0] = static_cast<std::uint8_t>(static_cast<std::int8_t>(value));
    } else if (width == 2) {
        store_u16(dest, static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
    } else if (width == 4) {
        store_u32(dest, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
    } else {
        store_u64(dest, static_cast<std::uint64_t>(value));
    }
}

void write_at(std::uint8_t* dest, Kind kind, const Value& value) {
    const int width = scalar_width(kind);
    if (kind == Kind::F32) {
        store_u32(dest, value.f32_bits());
        return;
    }
    if (kind == Kind::F64) {
        store_u64(dest, value.f64_bits());
        return;
    }
    if (is_signed(kind)) {
        std::int64_t number = 0;
        if (!value.fit_i64(number)) {
            throw Error("TYPE");
        }
        put_signed(dest, number, width);
        return;
    }
    std::uint64_t number = 0;
    if (!value.fit_u64(number)) {
        throw Error("TYPE");
    }
    put_unsigned(dest, number, width);
}

void put_unsigned(std::uint8_t* dest, std::uint64_t value, int width) {
    const std::uint64_t max = width == 8 ? UINT64_MAX : (std::uint64_t{1} << (width * 8)) - 1;
    if (value > max) {
        throw Error("TYPE");
    }
    if (width == 1) {
        dest[0] = static_cast<std::uint8_t>(value);
    } else if (width == 2) {
        store_u16(dest, static_cast<std::uint16_t>(value));
    } else if (width == 4) {
        store_u32(dest, static_cast<std::uint32_t>(value));
    } else {
        store_u64(dest, value);
    }
}

}  // namespace

Writer::Writer(std::size_t capacity) {
    buf_.resize(capacity == 0 ? 64 : capacity);
}

std::uint8_t* Writer::alloc(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() - end_) {
        throw Error("LIMIT");
    }
    const std::size_t need = end_ + n;
    if (need > buf_.size()) {
        std::size_t grown = buf_.size() * 2;
        if (grown < need) {
            grown = need;
        }
        buf_.resize(grown);
    }
    std::uint8_t* dest = buf_.data() + end_;
    end_ += n;
    return dest;
}

void Writer::write_counted(const std::uint8_t* data, std::size_t n, std::uint64_t cap) {
    if (n > cap || n > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    store_u32(alloc(4), static_cast<std::uint32_t>(n));
    if (n != 0) {
        std::memcpy(alloc(n), data, n);
    }
}

void Writer::write_leaf(Kind kind, const Value& value, std::uint64_t cap) {
    if (kind == Kind::Bool) {
        alloc(1)[0] = value.as_bool() ? 1 : 0;
        return;
    }
    const int width = scalar_width(kind);
    if (width > 0 && kind != Kind::F32 && kind != Kind::F64) {
        std::uint8_t* dest = alloc(static_cast<std::size_t>(width));
        if (is_signed(kind)) {
            std::int64_t number = 0;
            if (!value.fit_i64(number)) {
                throw Error("TYPE");
            }
            put_signed(dest, number, width);
            return;
        }
        std::uint64_t number = 0;
        if (!value.fit_u64(number)) {
            throw Error("TYPE");
        }
        put_unsigned(dest, number, width);
        return;
    }
    if (kind == Kind::F32) {
        store_u32(alloc(4), value.f32_bits());
        return;
    }
    if (kind == Kind::F64) {
        store_u64(alloc(8), value.f64_bits());
        return;
    }
    if (kind == Kind::Str) {
        const std::string& text = value.as_str();
        write_counted(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), cap);
        return;
    }
    if (kind == Kind::Bytes) {
        const auto& bytes = value.as_bytes();
        write_counted(bytes.data(), bytes.size(), cap);
        return;
    }
    throw Error("TYPE");
}

void Writer::write_list(const Schema& elem, const Value& value, std::uint64_t cap, int depth) {
    if (elem.kind == Kind::U8 && value.tag() == Value::Tag::Bytes) {
        const auto& bytes = value.as_bytes();
        fit_count(bytes.size(), 1, UINT64_MAX, cap);
        write_counted(bytes.data(), bytes.size(), cap);
        return;
    }
    if (value.tag() != Value::Tag::List) {
        throw Error("TYPE");
    }
    const auto& items = value.as_list();
    fit_count(items.size(), static_cast<std::uint64_t>(min_size(elem)), UINT64_MAX, cap);
    if (items.size() > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    store_u32(alloc(4), static_cast<std::uint32_t>(items.size()));
    if (elem.kind == Kind::U8 || elem.kind == Kind::I8) {
        if (items.size() > cap) {
            throw Error("LIMIT");
        }
        std::uint8_t* dest = items.empty() ? nullptr : alloc(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::int64_t number = 0;
            if (elem.kind == Kind::U8) {
                std::uint64_t u = 0;
                if (!items[i].fit_u64(u) || u > 255) {
                    throw Error("TYPE");
                }
                dest[i] = static_cast<std::uint8_t>(u);
            } else {
                if (!items[i].fit_i64(number)) {
                    throw Error("TYPE");
                }
                put_signed(dest + i, number, 1);
            }
        }
        return;
    }
    const int width = scalar_width(elem.kind);
    if (width > 0 && elem.kind != Kind::Bool) {
        std::uint8_t* dest = items.empty() ? nullptr : alloc(items.size() * static_cast<std::size_t>(width));
        for (std::size_t i = 0; i < items.size(); ++i) {
            write_at(dest + i * static_cast<std::size_t>(width), elem.kind, items[i]);
        }
        return;
    }
    for (const Value& item : items) {
        write_value(elem, item, cap, depth + 1);
    }
}

}  // namespace kiln
