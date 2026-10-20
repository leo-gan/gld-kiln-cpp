#include "tensor_internal.hpp"

#include "kiln/error.hpp"

#include <utility>
#include <vector>

namespace kiln {
namespace {

bool all_zero(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

bool packed_dtype(std::uint16_t dtype) { return dtype == 16 || dtype == 17 || dtype == 18; }

struct TensorVecPool {
    static std::vector<std::vector<Tensor>>& slots() {
        thread_local std::vector<std::vector<Tensor>> items;
        return items;
    }
    static std::vector<Tensor> take() {
        auto& items = slots();
        if (items.empty()) {
            return {};
        }
        std::vector<Tensor> value = std::move(items.back());
        items.pop_back();
        value.clear();
        return value;
    }
    static void give(std::vector<Tensor>&& value) noexcept {
        if (value.capacity() == 0) {
            return;
        }
        try {
            value.clear();
            auto& items = slots();
            if (items.size() < 8) {
                items.push_back(std::move(value));
            }
        } catch (...) {
        }
    }
};

void check_payload(std::vector<Tensor>& items, const std::uint8_t* payload, std::uint64_t payload_bytes, bool trusted) {
    thread_local std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    ranges.clear();
    if (ranges.capacity() < items.size()) {
        ranges.reserve(items.size());
    }
    for (Tensor& item : items) {
        const std::uint64_t item_end = add_u64(item.byte_offset, item.byte_length);
        if (item_end > payload_bytes) {
            throw Error("TYPE");
        }
        ranges.emplace_back(item.byte_offset, item_end);
        if (!trusted) {
            check_tensor(item);
        }
    }
    for (std::size_t left = 0; left < ranges.size(); ++left) {
        for (std::size_t right = left + 1; right < ranges.size(); ++right) {
            const auto [a0, a1] = ranges[left];
            const auto [b0, b1] = ranges[right];
            if (a0 == a1 || b0 == b1) {
                continue;
            }
            if (a0 < b1 && b0 < a1) {
                throw Error("TYPE");
            }
        }
    }
    std::uint64_t end = 0;
    for (const Tensor& item : items) {
        if (item.byte_offset != align64(end)) {
            throw Error("TYPE");
        }
        end = item.byte_offset + item.byte_length;
    }
    if (end != payload_bytes) {
        throw Error("TYPE");
    }
    for (Tensor& item : items) {
        item.data = ByteView(payload + item.byte_offset, static_cast<std::size_t>(item.byte_length));
        if (packed_dtype(item.dtype)) {
            const std::uint64_t count = element_count(item.rank, item.shape);
            if ((count & 1u) == 1 && item.data.size != 0 && (item.data.data[item.data.size - 1] >> 4) != 0) {
                throw Error("TYPE");
            }
        }
        if (item.dtype == 7) {
            for (std::size_t i = 0; i < item.data.size; ++i) {
                if (item.data.data[i] > 1) {
                    throw Error("TYPE");
                }
            }
        }
    }
    std::uint64_t cursor = 0;
    for (const Tensor& item : items) {
        if (!all_zero(payload + cursor, static_cast<std::size_t>(item.byte_offset - cursor))) {
            throw Error("TYPE");
        }
        cursor = item.byte_offset + item.byte_length;
    }
    if (!all_zero(payload + cursor, static_cast<std::size_t>(payload_bytes - cursor))) {
        throw Error("TYPE");
    }
    if (!trusted) {
        check_quant(items);
    }
}

}  // namespace

TensorDirectory decode_tensors(std::span<const std::uint8_t> buf, const ReadOpts& opts) {
    if (buf.size() < 4) {
        throw Error("EOF");
    }
    if (buf[0] != 0x4B || buf[1] != 0x4C || buf[2] != 0x4E || buf[3] != 0x54) {
        throw Error("FRAME");
    }
    if (buf.size() < kHeader) {
        throw Error("EOF");
    }
    const std::uint8_t* raw = buf.data();
    const std::uint8_t version = raw[4];
    const std::uint8_t flags = raw[5];
    if (version != 2 || (flags & ~0x03) != 0 || (flags & 0x02) == 0 || load_u16(raw + 6) != 0 || !all_zero(raw + 32, 32)) {
        throw Error("FRAME");
    }
    const std::uint32_t count = load_u32(raw + 8);
    const std::uint32_t name_bytes = load_u32(raw + 12);
    const std::uint64_t payload_bytes = load_u64(raw + 16);
    const std::uint64_t directory_bytes = load_u64(raw + 24);
    if (count > opts.cap || static_cast<std::uint64_t>(name_bytes) > opts.cap) {
        throw Error("LIMIT");
    }
    if (directory_bytes != add_u64(mul_u64(count, kDesc), name_bytes)) {
        throw Error("TYPE");
    }
    const std::uint64_t directory_end = add_u64(kHeader, directory_bytes);
    if (directory_end > buf.size()) {
        throw Error("EOF");
    }
    const std::uint64_t payload_base = align64(directory_end);
    const std::uint64_t block_end = add_u64(payload_base, payload_bytes);
    if (block_end > buf.size()) {
        throw Error("EOF");
    }
    if (!all_zero(raw + directory_end, static_cast<std::size_t>(payload_base - directory_end))) {
        throw Error("TYPE");
    }
    thread_local std::vector<NameRef> refs;
    refs.clear();
    if (refs.capacity() < count) {
        refs.reserve(count);
    }
    TensorDirectory directory;
    directory.tensors = TensorVecPool::take();
    if (directory.tensors.capacity() < count) {
        directory.tensors.reserve(count);
    }
    std::vector<Tensor>& items = directory.tensors;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint8_t* base = raw + kHeader + static_cast<std::size_t>(index) * kDesc;
        if (!all_zero(base + 112, 16)) {
            throw Error("FRAME");
        }
        Tensor item;
        item.dtype = load_u16(base);
        item.rank = base[2];
        item.block_size = load_u32(base + 12);
        item.quant_scheme = load_u16(base + 16);
        item.quant_axis = load_u16(base + 18);
        item.group_size = load_u32(base + 20);
        item.scale_index = load_u32(base + 24);
        item.outer_index = load_u32(base + 28);
        item.byte_offset = load_u64(base + 32);
        item.byte_length = load_u64(base + 40);
        for (int dim = 0; dim < 8; ++dim) {
            item.shape[dim] = load_u64(base + 48 + dim * 8);
        }
        NameRef ref;
        ref.reserved = base[3];
        ref.name_off = load_u16(base + 4);
        ref.name_len = load_u16(base + 6);
        ref.layout_off = load_u16(base + 8);
        ref.layout_len = load_u16(base + 10);
        items.push_back(std::move(item));
        refs.push_back(ref);
    }
    const std::uint8_t* heap = raw + kHeader + static_cast<std::size_t>(count) * kDesc;
    bind_tensor_names(items, refs, heap, name_bytes, flags, opts.trusted);
    check_payload(items, raw + payload_base, payload_bytes, opts.trusted);
    if (opts.standalone && block_end != buf.size()) {
        throw Error("TRAILING");
    }
    directory.version = 2;
    directory.has_names = (flags & 0x01) != 0;
    directory.block_length = block_end;
    return directory;
}

TensorDirectory::~TensorDirectory() { TensorVecPool::give(std::move(tensors)); }

}  // namespace kiln
