#pragma once

#include "kiln/schema.hpp"
#include "kiln/tensor.hpp"
#include "kiln/value.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kiln {

class Writer {
 public:
    explicit Writer(std::size_t capacity = 64);
    std::span<const std::uint8_t> encode(const Schema& schema, const Value& value, std::uint64_t cap = kCap);
    std::span<const std::uint8_t> encode_framed(const Schema& schema, const Value& value, std::uint64_t cap = kCap);
    std::span<const std::uint8_t> bytes() const { return {buf_.data(), buf_.size()}; }
    std::size_t size() const { return buf_.size(); }
    // Moves the encoded bytes out. The writer is empty afterwards.
    std::vector<std::uint8_t> release();

 private:
    std::vector<std::uint8_t> buf_;

    std::uint8_t* alloc(std::size_t n);
    void append_copy(const std::uint8_t* data, std::size_t n);
    void write_value(const Schema& schema, const Value& value, std::uint64_t cap, int depth);
    void write_leaf(Kind kind, const Value& value, std::uint64_t cap);
    void write_counted(const std::uint8_t* data, std::size_t n, std::uint64_t cap);
    void write_list(const Schema& elem, const Value& value, std::uint64_t cap, int depth);
};

std::vector<std::uint8_t> encode(const Schema& schema, const Value& value, std::uint64_t cap = kCap);
std::vector<std::uint8_t> encode_framed(const Schema& schema, const Value& value, std::uint64_t cap = kCap);
Value decode(const Schema& schema, std::span<const std::uint8_t> buf, const ReadOpts& opts = {});
void decode_into(const Schema& schema, std::span<const std::uint8_t> buf, Value& out, const ReadOpts& opts = {});
Value decode_framed(const Schema& schema, std::span<const std::uint8_t> buf, const ReadOpts& opts = {});

}  // namespace kiln
