#include "tensor_internal.hpp"

#include "kiln/error.hpp"

namespace kiln {
namespace {

std::uint64_t expected_bytes(std::uint16_t dtype, std::uint64_t count) {
    for (const DtypeInfo& info : kDtypes) {
        if (info.code != dtype) {
            continue;
        }
        if (info.packed) {
            return count / 2 + (count & 1u);
        }
        return mul_u64(count, info.width);
    }
    throw Error("TYPE");
}

bool named(const std::optional<std::string>& text) { return text && !text->empty(); }

}  // namespace

std::uint64_t element_count(std::uint8_t rank, const std::uint64_t* shape) {
    if (rank == 0) {
        return 1;
    }
    std::uint64_t count = 1;
    for (std::uint8_t dim = 0; dim < rank; ++dim) {
        count = mul_u64(count, shape[dim]);
    }
    return count;
}

void check_tensor(const Tensor& item) {
    if (item.rank > 8) {
        throw Error("TYPE");
    }
    for (int dim = item.rank; dim < 8; ++dim) {
        if (item.shape[dim] != 0) {
            throw Error("TYPE");
        }
    }
    const std::uint16_t dtype = item.dtype;
    if (dtype == 0 || (dtype >= 20 && dtype < 4096) || dtype > 32767) {
        throw Error("TYPE");
    }
    const std::uint64_t count = element_count(item.rank, item.shape);
    if (dtype >= 4096) {
        if (!named(item.layout) || item.block_size == 0 || item.byte_length % item.block_size != 0) {
            throw Error("TYPE");
        }
        return;
    }
    if (named(item.layout) || item.block_size != 0) {
        throw Error("TYPE");
    }
    if (item.byte_length != expected_bytes(dtype, count)) {
        throw Error("TYPE");
    }
}

void check_quant(const std::vector<Tensor>& items) {
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Tensor& item = items[index];
        const std::uint16_t scheme = item.quant_scheme;
        if (scheme == 0) {
            if (item.group_size != 0 || item.quant_axis != 0xFFFF || item.scale_index != 0xFFFFFFFFu ||
                item.outer_index != 0xFFFFFFFFu) {
                throw Error("TYPE");
            }
            continue;
        }
        if (scheme != 1 && scheme != 2) {
            throw Error("TYPE");
        }
        if (item.rank == 0 || item.quant_axis >= item.rank || item.group_size == 0) {
            throw Error("TYPE");
        }
        const std::uint32_t scale = item.scale_index;
        if (scale == index || scale >= items.size()) {
            throw Error("TYPE");
        }
        const Tensor& other = items[scale];
        if (other.quant_scheme != 0) {
            throw Error("TYPE");
        }
        const std::uint64_t axis = item.shape[item.quant_axis];
        const std::uint64_t groups = axis / item.group_size + (axis % item.group_size != 0 ? 1 : 0);
        std::uint64_t other_dims = 1;
        for (std::uint8_t dim = 0; dim < item.rank; ++dim) {
            if (dim != item.quant_axis) {
                other_dims = mul_u64(other_dims, item.shape[dim]);
            }
        }
        if (element_count(other.rank, other.shape) != mul_u64(groups, other_dims)) {
            throw Error("TYPE");
        }
        if (scheme == 1) {
            if (item.outer_index != 0xFFFFFFFFu) {
                throw Error("TYPE");
            }
            continue;
        }
        const std::uint32_t outer = item.outer_index;
        if (outer == index || outer == scale || outer >= items.size()) {
            throw Error("TYPE");
        }
        const Tensor& outer_item = items[outer];
        if (outer_item.quant_scheme != 0 || outer_item.dtype != 2 || element_count(outer_item.rank, outer_item.shape) != 1) {
            throw Error("TYPE");
        }
    }
}

}  // namespace kiln
