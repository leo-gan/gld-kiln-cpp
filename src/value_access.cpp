#include "kiln/value.hpp"

#include "pod_box.hpp"
#include "pool.hpp"

#include "kiln/error.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace kiln {

bool Value::as_bool() const {
    if (tag_ != Tag::Bool) {
        throw Error("TYPE");
    }
    return scalar_ != 0;
}

std::int64_t Value::as_i64() const {
    if (tag_ != Tag::I64) {
        throw Error("TYPE");
    }
    std::int64_t value = 0;
    std::memcpy(&value, &scalar_, 8);
    return value;
}

std::uint64_t Value::as_u64() const {
    if (tag_ != Tag::U64) {
        throw Error("TYPE");
    }
    return scalar_;
}

std::uint32_t Value::f32_bits() const {
    if (tag_ != Tag::F32) {
        throw Error("TYPE");
    }
    return static_cast<std::uint32_t>(scalar_);
}

float Value::as_f32() const {
    const std::uint32_t bits = f32_bits();
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

std::uint64_t Value::f64_bits() const {
    if (tag_ != Tag::F64) {
        throw Error("TYPE");
    }
    return scalar_;
}

double Value::as_f64() const {
    const std::uint64_t bits = f64_bits();
    double value = 0;
    std::memcpy(&value, &bits, 8);
    return value;
}

const std::string& Value::as_str() const {
    if (tag_ != Tag::Str) {
        throw Error("TYPE");
    }
    return *static_cast<const std::string*>(ptr_);
}

const std::vector<std::uint8_t>& Value::as_bytes() const {
    if (tag_ != Tag::Bytes) {
        throw Error("TYPE");
    }
    return *static_cast<const std::vector<std::uint8_t>*>(ptr_);
}

const Value::List& Value::as_list() const {
    if (tag_ != Tag::List) {
        throw Error("TYPE");
    }
    return *static_cast<const List*>(ptr_);
}

const Value::Dict& Value::as_dict() const {
    if (tag_ != Tag::Dict) {
        throw Error("TYPE");
    }
    return *static_cast<const Dict*>(ptr_);
}

const TensorDirectory& Value::as_tensors() const {
    if (tag_ != Tag::Tensors) {
        throw Error("TYPE");
    }
    return *static_cast<const TensorDirectory*>(ptr_);
}

const Value* Value::find(std::string_view key) const {
    if (tag_ != Tag::Dict) {
        return nullptr;
    }
    for (const auto& item : as_dict()) {
        if (item.first == key) {
            return &item.second;
        }
    }
    return nullptr;
}

bool Value::fit_i64(std::int64_t& out) const {
    if (tag_ == Tag::I64) {
        out = as_i64();
        return true;
    }
    if (tag_ == Tag::U64 && scalar_ <= static_cast<std::uint64_t>(INT64_MAX)) {
        out = static_cast<std::int64_t>(scalar_);
        return true;
    }
    return false;
}

Value::Dict& Value::scratch_dict() {
    if (tag_ == Tag::Dict) {
        return *static_cast<Dict*>(ptr_);
    }
    clear();
    Dict* dict = Pool<Dict>::take();
    tag_ = Tag::Dict;
    ptr_ = dict;
    return *dict;
}

bool Value::fit_u64(std::uint64_t& out) const {
    if (tag_ == Tag::U64) {
        out = scalar_;
        return true;
    }
    if (tag_ == Tag::I64) {
        const std::int64_t value = as_i64();
        if (value < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(value);
        return true;
    }
    return false;
}

namespace {

constexpr int kPoolMax = 8;

struct PoolState {
    PodBox* items[kPoolMax] = {};
    int n = 0;
};

PoolState& pool_state() {
    thread_local PoolState state;
    return state;
}

bool cap_fits(std::size_t cap, std::size_t count) {
    if (cap < count) {
        return false;
    }
    const std::size_t slack = count / 8 + 64;
    return cap - count <= slack;
}

PodBox* take_box(PodCode code, std::size_t count) {
    PoolState& state = pool_state();
    int best = -1;
    for (int i = 0; i < state.n; ++i) {
        PodBox* candidate = state.items[i];
        if (candidate->code == code && candidate->owned && candidate->hold == nullptr && cap_fits(candidate->cap, count)) {
            if (best < 0 || candidate->cap < state.items[best]->cap) {
                best = i;
            }
        }
    }
    if (best >= 0) {
        PodBox* box = state.items[best];
        state.items[best] = state.items[--state.n];
        box->count = count;
        return box;
    }
    auto* box = new PodBox;
    box->code = code;
    box->owned = true;
    box->count = count;
    box->cap = count;
    if (count != 0) {
        box->data = static_cast<std::uint8_t*>(std::malloc(count * static_cast<std::size_t>(pod_width(code))));
        if (box->data == nullptr) {
            delete box;
            throw std::bad_alloc();
        }
    }
    return box;
}

template <typename T>
std::span<const T> span_of(const Value& value, PodCode code) {
    const PodBox* item = PodOps::box(value, code);
    if (item->count == 0) {
        return {};
    }
    return {reinterpret_cast<const T*>(item->data), item->count};
}

Value make_view(PodCode code, const void* data, std::size_t count) {
    if (count != 0 && data == nullptr) {
        throw Error("TYPE");
    }
    return PodOps::adopt_view(code, data, count);
}

}  // namespace

Value PodOps::uninit(PodCode code, std::size_t count) {
    PodBox* box = take_box(code, count);
    Value out;
    out.tag_ = Value::Tag::Pod;
    out.scalar_ = 0;
    out.ptr_ = box;
    return out;
}

void PodOps::fill(Value& slot, PodCode code, const std::uint8_t* src, std::size_t count) {
    const int width = pod_width(code);
    if (slot.tag_ == Value::Tag::Pod) {
        auto* box = static_cast<PodBox*>(slot.ptr_);
        if (box->owned && box->hold == nullptr && box->code == code) {
            if (box->cap < count) {
                std::uint8_t* memory = nullptr;
                if (count != 0) {
                    memory = static_cast<std::uint8_t*>(std::malloc(count * static_cast<std::size_t>(width)));
                    if (memory == nullptr) {
                        throw std::bad_alloc();
                    }
                }
                std::free(box->data);
                box->data = memory;
                box->cap = count;
            }
            box->count = count;
            if (count != 0) {
                copy_le_to_host(box->data, src, count, width);
            }
            return;
        }
    }
    Value created = uninit(code, count);
    if (count != 0) {
        copy_le_to_host(static_cast<PodBox*>(created.ptr_)->data, src, count, width);
    }
    slot = std::move(created);
}

bool PodOps::try_view(const Value& value, PodCode code, const std::uint8_t*& data, std::size_t& count) {
    if (value.tag_ != Value::Tag::Pod) {
        return false;
    }
    const auto* item = static_cast<const PodBox*>(value.ptr_);
    if (item->code != code) {
        throw Error("TYPE");
    }
    data = item->data;
    count = item->count;
    return true;
}

const PodBox* PodOps::box(const Value& value, PodCode code) {
    if (value.tag_ != Value::Tag::Pod || value.ptr_ == nullptr) {
        throw Error("TYPE");
    }
    const auto* item = static_cast<const PodBox*>(value.ptr_);
    if (item->code != code) {
        throw Error("TYPE");
    }
    return item;
}

PodBox* PodOps::clone(const PodBox* src) {
    if (src == nullptr) {
        return nullptr;
    }
    PodBox* box = take_box(src->code, src->count);
    if (src->count != 0 && src->data != nullptr) {
        std::memcpy(box->data, src->data, src->count * static_cast<std::size_t>(pod_width(src->code)));
    }
    return box;
}

void PodOps::release(PodBox* box) noexcept {
    if (box == nullptr) {
        return;
    }
    if (box->hold != nullptr || !box->owned) {
        delete box->hold;
        delete box;
        return;
    }
    try {
        PoolState& state = pool_state();
        if (state.n < kPoolMax) {
            state.items[state.n++] = box;
            return;
        }
    } catch (...) {
    }
    std::free(box->data);
    delete box;
}

Value PodOps::adopt_view(PodCode code, const void* data, std::size_t count) {
    auto* box = new PodBox;
    box->code = code;
    box->owned = false;
    box->data = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(data));
    box->count = count;
    Value out;
    out.tag_ = Value::Tag::Pod;
    out.scalar_ = 0;
    out.ptr_ = box;
    return out;
}

PodKind Value::pod_kind() const noexcept {
    if (tag_ != Tag::Pod || ptr_ == nullptr) {
        return PodKind::None;
    }
    switch (static_cast<const PodBox*>(ptr_)->code) {
        case PodCode::F32:
            return PodKind::F32;
        case PodCode::F64:
            return PodKind::F64;
        case PodCode::I32:
            return PodKind::I32;
        case PodCode::I64:
            return PodKind::I64;
        case PodCode::U64:
            return PodKind::U64;
        default:
            return PodKind::None;
    }
}

std::span<const float> Value::as_f32s() const { return span_of<float>(*this, PodCode::F32); }
std::span<const double> Value::as_f64s() const { return span_of<double>(*this, PodCode::F64); }
std::span<const std::int32_t> Value::as_i32s() const { return span_of<std::int32_t>(*this, PodCode::I32); }
std::span<const std::int64_t> Value::as_i64s() const { return span_of<std::int64_t>(*this, PodCode::I64); }
std::span<const std::uint64_t> Value::as_u64s() const { return span_of<std::uint64_t>(*this, PodCode::U64); }

Value Value::f32s(std::vector<float> values) { return PodOps::adopt_vec<float>(PodCode::F32, std::move(values)); }
Value Value::f64s(std::vector<double> values) { return PodOps::adopt_vec<double>(PodCode::F64, std::move(values)); }
Value Value::i32s(std::vector<std::int32_t> values) { return PodOps::adopt_vec<std::int32_t>(PodCode::I32, std::move(values)); }
Value Value::i64s(std::vector<std::int64_t> values) { return PodOps::adopt_vec<std::int64_t>(PodCode::I64, std::move(values)); }
Value Value::u64s(std::vector<std::uint64_t> values) { return PodOps::adopt_vec<std::uint64_t>(PodCode::U64, std::move(values)); }

Value Value::f32_view(const float* data, std::size_t count) { return make_view(PodCode::F32, data, count); }
Value Value::f64_view(const double* data, std::size_t count) { return make_view(PodCode::F64, data, count); }
Value Value::i32_view(const std::int32_t* data, std::size_t count) { return make_view(PodCode::I32, data, count); }
Value Value::i64_view(const std::int64_t* data, std::size_t count) { return make_view(PodCode::I64, data, count); }
Value Value::u64_view(const std::uint64_t* data, std::size_t count) { return make_view(PodCode::U64, data, count); }

}  // namespace kiln
