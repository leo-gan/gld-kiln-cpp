#pragma once

#include "kiln/bits.hpp"
#include "kiln/schema.hpp"
#include "kiln/value.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiln {

enum class PodCode : std::uint8_t { None = 0, F32 = 1, F64 = 2, I32 = 3, I64 = 4, U64 = 5 };

struct VecHold {
    virtual ~VecHold() = default;
};

template <typename T>
struct VecHoldT : VecHold {
    std::vector<T> values;
    explicit VecHoldT(std::vector<T>&& items) : values(std::move(items)) {}
};

// Owned buffers are malloc'd and recycled. A view borrows the caller. A hold
// owns a std::vector the caller moved in. width is 4 or 8, never a wider slot.
struct PodBox {
    std::uint8_t* data = nullptr;
    std::size_t count = 0;
    std::size_t cap = 0;
    PodCode code = PodCode::None;
    bool owned = false;
    VecHold* hold = nullptr;
};

inline int pod_width(PodCode code) {
    return (code == PodCode::F32 || code == PodCode::I32) ? 4 : 8;
}

inline PodCode pod_code(Kind kind) {
    switch (kind) {
        case Kind::F32:
            return PodCode::F32;
        case Kind::F64:
            return PodCode::F64;
        case Kind::I32:
            return PodCode::I32;
        case Kind::I64:
        case Kind::Int:
            return PodCode::I64;
        case Kind::U64:
        case Kind::Uint:
            return PodCode::U64;
        default:
            return PodCode::None;
    }
}

struct PodOps {
    static Value uninit(PodCode code, std::size_t count);
    static void fill(Value& slot, PodCode code, const std::uint8_t* src, std::size_t count);
    static bool try_view(const Value& value, PodCode code, const std::uint8_t*& data, std::size_t& count);
    static const PodBox* box(const Value& value, PodCode code);
    static PodBox* clone(const PodBox* src);
    static void release(PodBox* box) noexcept;
    static Value adopt_view(PodCode code, const void* data, std::size_t count);

    template <typename T>
    static Value adopt_vec(PodCode code, std::vector<T>&& values);
};

template <typename T>
Value PodOps::adopt_vec(PodCode code, std::vector<T>&& values) {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "pod width");
    const std::size_t count = values.size();
    auto* hold = new VecHoldT<T>(std::move(values));
    if (sizeof(T) != static_cast<std::size_t>(pod_width(code))) {
        delete hold;
        throw Error("TYPE");
    }
    auto* box = new PodBox;
    box->code = code;
    box->owned = true;
    box->hold = hold;
    box->count = count;
    box->cap = hold->values.capacity();
    box->data = reinterpret_cast<std::uint8_t*>(hold->values.data());
    Value out;
    out.tag_ = Value::Tag::Pod;
    out.scalar_ = 0;
    out.ptr_ = box;
    return out;
}

}  // namespace kiln
