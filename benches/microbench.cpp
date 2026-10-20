#include "kiln/frozen.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

template <typename Fn>
std::uint64_t bench(Fn&& fn, int n, int warm) {
    for (int i = 0; i < warm; ++i) {
        fn();
    }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        fn();
    }
    const auto stop = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()) /
           static_cast<std::uint64_t>(n);
}

}  // namespace

int main() {
    const kiln::Schema point = kiln::struct_of({
        {"x", kiln::scalar(kiln::Kind::I32)},
        {"y", kiln::scalar(kiln::Kind::I32)},
        {"label", kiln::scalar(kiln::Kind::Str)},
    });
    const kiln::Value value = kiln::Value::dict({
        {"x", kiln::Value::i64(9)},
        {"y", kiln::Value::i64(-4)},
        {"label", kiln::Value::str("item")},
    });
    std::vector<std::uint8_t> payload(1024 * 4, 0);
    for (int i = 0; i < 1024; ++i) {
        const float sample = 1.0f;
        std::memcpy(payload.data() + static_cast<std::size_t>(i) * 4, &sample, 4);
    }
    kiln::Tensor tensor;
    tensor.dtype = kiln::dtype_from_name("f32");
    kiln::set_shape(tensor, {1024});
    tensor.data = kiln::ByteView(payload.data(), payload.size());
    const std::vector<kiln::Tensor> tensors = {tensor};

    kiln::Writer writer(256);
    writer.encode(point, value);
    const auto encoded = writer.bytes();
    std::vector<std::uint8_t> block;
    kiln::encode_tensors_into(tensors, block);

    std::cout << "struct_encode_ns " << bench([&] { writer.encode(point, value); }, 2000, 100) << "\n";
    std::cout << "struct_decode_ns " << bench([&] { (void)kiln::decode(point, writer.bytes()); }, 2000, 100) << "\n";
    std::cout << "struct_bytes " << encoded.size() << "\n";
    std::cout << "tensor_encode_ns " << bench([&] { kiln::encode_tensors_into(tensors, block); }, 200, 20) << "\n";
    std::cout << "tensor_decode_ns " << bench([&] { (void)kiln::decode_tensors(block); }, 200, 20) << "\n";
    std::cout << "tensor_bytes " << block.size() << "\n";
    return 0;
}
