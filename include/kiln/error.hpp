#pragma once

#include <cstddef>
#include <stdexcept>

namespace kiln {

// Wire error names: FRAME, LIMIT, EOF, TYPE, UTF8, TRAILING, SCHEMA.
class Error : public std::runtime_error {
 public:
    explicit Error(const char* code, std::size_t offset = 0)
        : std::runtime_error(code), code_(code), offset_(offset) {}

    const char* code() const noexcept { return code_; }
    std::size_t offset() const noexcept { return offset_; }

 private:
    const char* code_;
    std::size_t offset_;
};

}  // namespace kiln
