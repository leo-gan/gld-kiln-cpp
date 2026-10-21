#include "kiln/tensor.hpp"

#include "kiln/error.hpp"

#include <cstring>
#include <string>

namespace kiln {
namespace {

struct Prep {
    const Tensor* tensor = nullptr;
    std::uint16_t name_off = 0xFFFF;
    std::uint16_t name_len = 0;
    std::uint16_t layout_off = 0xFFFF;
    std::uint16_t layout_len = 0;
    std::uint64_t byte_offset = 0;
};

void stamp_text(const std::optional<std::string>& text, std::uint16_t& off, std::uint16_t& len, std::uint64_t& heap) {
    if (text && !text->empty()) {
        if (heap > 0xFFFF || text->size() > 0xFFFF) {
            throw Error("LIMIT");
        }
        off = static_cast<std::uint16_t>(heap);
        len = static_cast<std::uint16_t>(text->size());
        heap += text->size();
        return;
    }
    off = 0xFFFF;
    len = 0;
}

void write_desc(std::uint8_t* base, const Prep& item) {
    const Tensor& tensor = *item.tensor;
    std::memset(base, 0, kDesc);
    store_u16(base + 0, tensor.dtype);
    base[2] = tensor.rank;
    store_u16(base + 4, item.name_off);
    store_u16(base + 6, item.name_len);
    store_u16(base + 8, item.layout_off);
    store_u16(base + 10, item.layout_len);
    store_u32(base + 12, tensor.block_size);
    store_u16(base + 16, tensor.quant_scheme);
    store_u16(base + 18, tensor.quant_axis);
    store_u32(base + 20, tensor.group_size);
    store_u32(base + 24, tensor.scale_index);
    store_u32(base + 28, tensor.outer_index);
    store_u64(base + 32, item.byte_offset);
    store_u64(base + 40, tensor.data.size);
    for (int dim = 0; dim < 8; ++dim) {
        store_u64(base + 48 + dim * 8, tensor.shape[dim]);
    }
}

}  // namespace

const char* dtype_name(std::uint16_t code) {
    for (const DtypeInfo& info : kDtypes) {
        if (info.code == code) {
            return info.name;
        }
    }
    return nullptr;
}

std::uint16_t dtype_from_name(std::string_view name) {
    for (const DtypeInfo& info : kDtypes) {
        if (name == info.name) {
            return info.code;
        }
    }
    throw Error("TYPE");
}

void set_shape(Tensor& tensor, std::initializer_list<std::uint64_t> dims) {
    if (dims.size() > 8) {
        throw Error("TYPE");
    }
    tensor.rank = static_cast<std::uint8_t>(dims.size());
    std::memset(tensor.shape, 0, sizeof(tensor.shape));
    std::size_t index = 0;
    for (std::uint64_t dim : dims) {
        tensor.shape[index++] = dim;
    }
}

void encode_tensors_into(const std::vector<Tensor>& tensors, std::vector<std::uint8_t>& out) {
    thread_local std::vector<Prep> prep;
    prep.clear();
    if (prep.capacity() < tensors.size()) {
        prep.reserve(tensors.size());
    }
    for (const Tensor& tensor : tensors) {
        if (tensor.rank > 8) {
            throw Error("TYPE");
        }
        if (tensor.data.size > 0 && tensor.data.data == nullptr) {
            throw Error("TYPE");
        }
        prep.push_back(Prep{&tensor});
    }
    std::uint64_t heap_size = 0;
    for (Prep& item : prep) {
        stamp_text(item.tensor->name, item.name_off, item.name_len, heap_size);
    }
    for (Prep& item : prep) {
        stamp_text(item.tensor->layout, item.layout_off, item.layout_len, heap_size);
    }
    std::uint64_t payload = 0;
    for (Prep& item : prep) {
        item.byte_offset = align64(payload);
        payload = add_u64(item.byte_offset, item.tensor->data.size);
    }
    const std::uint64_t count = prep.size();
    if (count > 0xFFFFFFFFu || heap_size > 0xFFFFFFFFu) {
        throw Error("LIMIT");
    }
    const std::uint64_t directory_bytes = add_u64(mul_u64(count, kDesc), heap_size);
    const std::uint64_t payload_base = align64(add_u64(kHeader, directory_bytes));
    const std::uint64_t block = add_u64(payload_base, payload);
    std::uint8_t flags = 0x02;
    for (const Prep& item : prep) {
        if (item.name_len != 0) {
            flags = static_cast<std::uint8_t>(flags | 0x01);
            break;
        }
    }
    if (out.capacity() < static_cast<std::size_t>(block)) {
        out.reserve(static_cast<std::size_t>(block));
    }
    const auto write_front = [&](std::uint8_t* raw) {
        if (payload_base != 0) {
            std::memset(raw, 0, static_cast<std::size_t>(payload_base));
        }
        std::memcpy(raw, "KLNT", 4);
        raw[4] = 2;
        raw[5] = flags;
        store_u32(raw + 8, static_cast<std::uint32_t>(count));
        store_u32(raw + 12, static_cast<std::uint32_t>(heap_size));
        store_u64(raw + 16, payload);
        store_u64(raw + 24, directory_bytes);
        for (std::size_t index = 0; index < prep.size(); ++index) {
            write_desc(raw + kHeader + index * kDesc, prep[index]);
        }
        std::size_t heap_at = static_cast<std::size_t>(kHeader + count * kDesc);
        auto append = [&](const std::optional<std::string>& text) {
            if (!text || text->empty()) {
                return;
            }
            std::memcpy(raw + heap_at, text->data(), text->size());
            heap_at += text->size();
        };
        for (const Prep& item : prep) {
            append(item.tensor->name);
        }
        for (const Prep& item : prep) {
            append(item.tensor->layout);
        }
    };
    const auto write_gaps_and_payload = [&](std::uint8_t* payload_bytes_ptr) {
        std::uint64_t cursor = 0;
        for (const Prep& item : prep) {
            if (item.byte_offset > cursor) {
                std::memset(payload_bytes_ptr + cursor, 0, static_cast<std::size_t>(item.byte_offset - cursor));
            }
            if (item.tensor->data.size != 0) {
                std::memcpy(payload_bytes_ptr + item.byte_offset, item.tensor->data.data, item.tensor->data.size);
            }
            cursor = add_u64(item.byte_offset, item.tensor->data.size);
        }
        if (cursor < payload) {
            std::memset(payload_bytes_ptr + cursor, 0, static_cast<std::size_t>(payload - cursor));
        }
    };
    if (out.size() == static_cast<std::size_t>(block)) {
        std::uint8_t* raw = out.data();
        write_front(raw);
        write_gaps_and_payload(raw + static_cast<std::size_t>(payload_base));
        return;
    }
    // Grow the header only. Payload bytes are copied, not value-initialized first.
    out.resize(static_cast<std::size_t>(payload_base));
    write_front(out.data());
    std::uint64_t cursor = 0;
    for (const Prep& item : prep) {
        if (item.byte_offset > cursor) {
            out.insert(out.end(), static_cast<std::size_t>(item.byte_offset - cursor), 0);
        }
        if (item.tensor->data.size != 0) {
            const std::uint8_t* bytes = item.tensor->data.data;
            out.insert(out.end(), bytes, bytes + item.tensor->data.size);
        }
        cursor = add_u64(item.byte_offset, item.tensor->data.size);
    }
    if (cursor < payload) {
        out.insert(out.end(), static_cast<std::size_t>(payload - cursor), 0);
    }
}

std::vector<std::uint8_t> encode_tensors(const std::vector<Tensor>& tensors) {
    std::vector<std::uint8_t> out;
    encode_tensors_into(tensors, out);
    return out;
}

}  // namespace kiln
