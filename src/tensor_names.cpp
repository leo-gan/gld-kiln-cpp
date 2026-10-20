#include "tensor_internal.hpp"

#include "kiln/error.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace kiln {
namespace {

std::string slice_text(const std::uint8_t* heap, std::uint64_t name_bytes, std::uint16_t off, std::uint16_t len, bool trusted) {
    const std::uint64_t end = add_u64(off, len);
    if (end > name_bytes) {
        throw Error("TYPE");
    }
    if (!trusted && !utf8_ok(heap + off, len)) {
        throw Error("UTF8");
    }
    return std::string(reinterpret_cast<const char*>(heap + off), len);
}

}  // namespace

void bind_tensor_names(std::vector<Tensor>& items, const std::vector<NameRef>& refs, const std::uint8_t* heap,
                       std::uint32_t name_bytes, std::uint8_t flags, bool trusted) {
    const bool names_flag = (flags & 0x01) != 0;
    bool has_name = false;
    for (const NameRef& ref : refs) {
        if (ref.name_len != 0) {
            has_name = true;
            break;
        }
    }
    if (names_flag != has_name || (names_flag && [&] {
            for (const NameRef& ref : refs) {
                if (ref.name_len == 0) {
                    return true;
                }
            }
            return false;
        }())) {
        throw Error("TYPE");
    }
    std::vector<std::string> parts;
    std::vector<std::string> layouts;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const NameRef& ref = refs[index];
        if (ref.reserved != 0) {
            throw Error("FRAME");
        }
        if (ref.name_len == 0) {
            if (ref.name_off != 0xFFFF) {
                throw Error("TYPE");
            }
        } else {
            if (ref.name_off == 0xFFFF) {
                throw Error("TYPE");
            }
            std::string name = slice_text(heap, name_bytes, ref.name_off, ref.name_len, trusted);
            for (const std::string& prev : parts) {
                if (prev == name) {
                    throw Error("TYPE");
                }
            }
            parts.push_back(name);
            items[index].name = std::move(name);
        }
        if (ref.layout_len == 0) {
            if (ref.layout_off != 0xFFFF) {
                throw Error("TYPE");
            }
        } else {
            std::string layout = slice_text(heap, name_bytes, ref.layout_off, ref.layout_len, trusted);
            layouts.push_back(layout);
            items[index].layout = std::move(layout);
        }
    }
    std::size_t cursor = 0;
    auto take = [&](const std::string& text) {
        if (cursor + text.size() > name_bytes || std::memcmp(heap + cursor, text.data(), text.size()) != 0) {
            throw Error("TYPE");
        }
        cursor += text.size();
    };
    for (const std::string& text : parts) {
        take(text);
    }
    for (const std::string& text : layouts) {
        take(text);
    }
    if (cursor != name_bytes) {
        throw Error("TYPE");
    }
}

}  // namespace kiln
