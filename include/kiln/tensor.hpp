#pragma once

#include "kiln/bits.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kiln {

struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ByteView() = default;
    ByteView(const std::uint8_t* pointer, std::size_t length) : data(pointer), size(length) {}
    explicit ByteView(std::span<const std::uint8_t> bytes) : data(bytes.data()), size(bytes.size()) {}

    std::span<const std::uint8_t> span() const { return {data, size}; }
};

struct Tensor {
    std::optional<std::string> name;
    std::uint16_t dtype = 0;
    std::uint8_t rank = 0;
    std::uint64_t shape[8] = {};
    ByteView data;
    std::uint16_t quant_scheme = 0;
    std::uint16_t quant_axis = 0xFFFF;
    std::uint32_t group_size = 0;
    std::uint32_t scale_index = 0xFFFFFFFFu;
    std::uint32_t outer_index = 0xFFFFFFFFu;
    std::optional<std::string> layout;
    std::uint32_t block_size = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_length = 0;
};

struct TensorDirectory {
    int version = 2;
    bool has_names = false;
    std::vector<Tensor> tensors;
    std::uint64_t block_length = 0;

    TensorDirectory() = default;
    TensorDirectory(const TensorDirectory&) = default;
    TensorDirectory(TensorDirectory&&) noexcept = default;
    TensorDirectory& operator=(const TensorDirectory&) = default;
    TensorDirectory& operator=(TensorDirectory&&) noexcept = default;
    ~TensorDirectory();
};

struct ReadOpts {
    std::uint64_t cap = kCap;
    bool standalone = true;
    bool trusted = false;
};

struct DtypeInfo {
    const char* name;
    std::uint16_t code;
    std::uint8_t width;
    bool packed;
};

inline constexpr DtypeInfo kDtypes[] = {
    {"f64", 1, 8, false},  {"f32", 2, 4, false},  {"f16", 3, 2, false},    {"bf16", 4, 2, false},
    {"f8e4m3", 5, 1, false}, {"f8e5m2", 6, 1, false}, {"bool", 7, 1, false}, {"i8", 8, 1, false},
    {"u8", 9, 1, false},   {"i16", 10, 2, false}, {"u16", 11, 2, false},   {"i32", 12, 4, false},
    {"u32", 13, 4, false}, {"i64", 14, 8, false}, {"u64", 15, 8, false},   {"i4", 16, 0, true},
    {"u4", 17, 0, true},   {"e2m1", 18, 0, true}, {"e8m0", 19, 1, false},
};

const char* dtype_name(std::uint16_t code);
std::uint16_t dtype_from_name(std::string_view name);

void set_shape(Tensor& tensor, std::initializer_list<std::uint64_t> dims);

std::vector<std::uint8_t> encode_tensors(const std::vector<Tensor>& tensors);
void encode_tensors_into(const std::vector<Tensor>& tensors, std::vector<std::uint8_t>& out);
TensorDirectory decode_tensors(std::span<const std::uint8_t> buf, const ReadOpts& opts = {});

}  // namespace kiln
