#pragma once

#include "kiln/tensor.hpp"

#include <vector>

namespace kiln {

struct NameRef {
    std::uint16_t name_off = 0;
    std::uint16_t name_len = 0;
    std::uint16_t layout_off = 0;
    std::uint16_t layout_len = 0;
    std::uint8_t reserved = 0;
};

std::uint64_t element_count(std::uint8_t rank, const std::uint64_t* shape);
void check_tensor(const Tensor& item);
void check_quant(const std::vector<Tensor>& items);
void bind_tensor_names(std::vector<Tensor>& items, const std::vector<NameRef>& refs, const std::uint8_t* heap,
                       std::uint32_t name_bytes, std::uint8_t flags, bool trusted);

}  // namespace kiln
