#include "kiln/value.hpp"

#include "pod_box.hpp"
#include "pool.hpp"

#include "kiln/error.hpp"

#include <cstring>
#include <utility>

namespace kiln {
namespace {

template <typename T>
T* as_ptr(void* pointer) {
    return static_cast<T*>(pointer);
}

}  // namespace

Value::~Value() { clear(); }

void Value::clear() noexcept {
    switch (tag_) {
        case Tag::Str: {
            std::string* text = as_ptr<std::string>(ptr_);
            text->clear();
            Pool<std::string>::give(text);
            break;
        }
        case Tag::Bytes:
            delete as_ptr<std::vector<std::uint8_t>>(ptr_);
            break;
        case Tag::List:
            delete as_ptr<List>(ptr_);
            break;
        case Tag::Dict:
            Pool<Dict>::give(as_ptr<Dict>(ptr_));
            break;
        case Tag::Tensors:
            delete as_ptr<TensorDirectory>(ptr_);
            break;
        case Tag::Pod:
            PodOps::release(static_cast<PodBox*>(ptr_));
            break;
        default:
            break;
    }
    tag_ = Tag::Null;
    scalar_ = 0;
    ptr_ = nullptr;
}
void Value::swap(Value& other) noexcept {
    std::swap(tag_, other.tag_);
    std::swap(scalar_, other.scalar_);
    std::swap(ptr_, other.ptr_);
}

Value::Value(const Value& other) : tag_(other.tag_), scalar_(other.scalar_) {
    switch (tag_) {
        case Tag::Str: {
            std::string* text = Pool<std::string>::take();
            *text = *static_cast<const std::string*>(other.ptr_);
            ptr_ = text;
            break;
        }
        case Tag::Bytes:
            ptr_ = new std::vector<std::uint8_t>(*static_cast<const std::vector<std::uint8_t>*>(other.ptr_));
            break;
        case Tag::List:
            ptr_ = new List(*static_cast<const List*>(other.ptr_));
            break;
        case Tag::Dict: {
            Dict* dict = Pool<Dict>::take();
            *dict = *static_cast<const Dict*>(other.ptr_);
            ptr_ = dict;
            break;
        }
        case Tag::Tensors:
            ptr_ = new TensorDirectory(*static_cast<const TensorDirectory*>(other.ptr_));
            break;
        case Tag::Pod:
            ptr_ = PodOps::clone(static_cast<const PodBox*>(other.ptr_));
            break;
        default:
            ptr_ = nullptr;
            break;
    }
}

Value::Dict& Value::reset_dict(std::size_t count) {
    Dict* dict = nullptr;
    if (tag_ == Tag::Dict) {
        dict = as_ptr<Dict>(ptr_);
    } else {
        clear();
        dict = Pool<Dict>::take();
        tag_ = Tag::Dict;
        ptr_ = dict;
    }
    dict->clear();
    if (dict->capacity() < count) {
        dict->reserve(count);
    }
    return *dict;
}

Value::Value(Value&& other) noexcept : tag_(other.tag_), scalar_(other.scalar_), ptr_(other.ptr_) {
    other.tag_ = Tag::Null;
    other.scalar_ = 0;
    other.ptr_ = nullptr;
}

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        Value copy(other);
        swap(copy);
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept {
    if (this != &other) {
        clear();
        tag_ = other.tag_;
        scalar_ = other.scalar_;
        ptr_ = other.ptr_;
        other.tag_ = Tag::Null;
        other.scalar_ = 0;
        other.ptr_ = nullptr;
    }
    return *this;
}

Value Value::null() { return Value(); }

Value Value::boolean(bool value) {
    Value out;
    out.tag_ = Tag::Bool;
    out.scalar_ = value ? 1 : 0;
    return out;
}

Value Value::i64(std::int64_t value) {
    Value out;
    out.tag_ = Tag::I64;
    std::memcpy(&out.scalar_, &value, 8);
    return out;
}

Value Value::u64(std::uint64_t value) {
    Value out;
    out.tag_ = Tag::U64;
    out.scalar_ = value;
    return out;
}

Value Value::f32_bits(std::uint32_t bits) {
    Value out;
    out.tag_ = Tag::F32;
    out.scalar_ = bits;
    return out;
}

Value Value::f32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return f32_bits(bits);
}

Value Value::f64_bits(std::uint64_t bits) {
    Value out;
    out.tag_ = Tag::F64;
    out.scalar_ = bits;
    return out;
}

Value Value::f64(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, 8);
    return f64_bits(bits);
}

Value Value::str(std::string value) {
    Value out;
    out.tag_ = Tag::Str;
    std::string* text = Pool<std::string>::take();
    *text = std::move(value);
    out.ptr_ = text;
    return out;
}

Value Value::bytes(std::vector<std::uint8_t> value) {
    Value out;
    out.tag_ = Tag::Bytes;
    out.ptr_ = new std::vector<std::uint8_t>(std::move(value));
    return out;
}

Value Value::list(List value) {
    Value out;
    out.tag_ = Tag::List;
    out.ptr_ = new List(std::move(value));
    return out;
}

Value Value::dict(Dict value) {
    Value out;
    out.tag_ = Tag::Dict;
    Dict* dict = Pool<Dict>::take();
    *dict = std::move(value);
    out.ptr_ = dict;
    return out;
}

Value Value::tensors(TensorDirectory value) {
    Value out;
    out.tag_ = Tag::Tensors;
    out.ptr_ = new TensorDirectory(std::move(value));
    return out;
}

}  // namespace kiln
