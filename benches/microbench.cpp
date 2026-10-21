#include "kiln/frozen.hpp"

#include <algorithm>
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

struct Sample {
    std::uint64_t median = 0;
    std::uint64_t iqr = 0;
};

template <typename Fn>
Sample bench_median(Fn&& fn, int n, int warm) {
    for (int i = 0; i < warm; ++i) {
        fn();
    }
    std::vector<std::uint64_t> samples(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        const auto stop = std::chrono::steady_clock::now();
        samples[static_cast<std::size_t>(i)] =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
    }
    std::sort(samples.begin(), samples.end());
    const std::uint64_t q1 = samples[static_cast<std::size_t>(n / 4)];
    const std::uint64_t q3 = samples[static_cast<std::size_t>((n * 3) / 4)];
    return Sample{samples[static_cast<std::size_t>(n / 2)], q3 - q1};
}

void report(const char* name, Sample sample) {
    std::cout << name << " " << sample.median << " iqr " << sample.iqr << "\n";
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

    constexpr std::size_t kCount = 1048576;
    std::vector<float> values(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        std::uint32_t bits = 0x3f800000u ^ static_cast<std::uint32_t>(i * 0x9E3779B9u);
        if (i == 0) {
            bits = 0x80000000u;
        } else if (i == 1) {
            bits = 0x7fc00000u;
        }
        std::memcpy(&values[i], &bits, 4);
    }
    const kiln::Schema matrix = kiln::struct_of({
        {"rows", kiln::scalar(kiln::Kind::I32)},
        {"cols", kiln::scalar(kiln::Kind::I32)},
        {"values", kiln::list_of(kiln::scalar(kiln::Kind::F32))},
    });
    const kiln::Schema f32_list = kiln::list_of(kiln::scalar(kiln::Kind::F32));
    const kiln::Value matrix_value = kiln::Value::dict({
        {"rows", kiln::Value::i64(1024)},
        {"cols", kiln::Value::i64(1024)},
        {"values", kiln::Value::f32_view(values.data(), values.size())},
    });
    const kiln::Value list_value = kiln::Value::f32_view(values.data(), values.size());
    kiln::Writer matrix_writer(12 + kCount * 4);
    kiln::Writer list_writer(4 + kCount * 4);
    std::vector<float> floor_dst(kCount);
    volatile std::uint32_t sink = 0;
    matrix_writer.encode(matrix, matrix_value);
    if (matrix_writer.size() != 12 + kCount * 4 ||
        std::memcmp(matrix_writer.bytes().data() + 12, values.data(), kCount * 4) != 0) {
        std::cerr << "matrix encode mismatch\n";
        return 1;
    }
    kiln::Value matrix_slot;
    kiln::decode_into(matrix, matrix_writer.bytes(), matrix_slot);
    const auto decoded = matrix_slot.find("values")->as_f32s();
    std::uint32_t bit0 = 0;
    std::uint32_t bit1 = 0;
    std::memcpy(&bit0, &decoded[0], 4);
    std::memcpy(&bit1, &decoded[1], 4);
    if (decoded.size() != kCount || bit0 != 0x80000000u || bit1 != 0x7fc00000u ||
        std::memcmp(decoded.data(), values.data(), kCount * 4) != 0) {
        std::cerr << "matrix decode mismatch\n";
        return 1;
    }
    report("f32_memcpy_floor_ns", bench_median([&] {
        std::memcpy(floor_dst.data(), values.data(), kCount * 4);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &floor_dst[0], 4);
        sink ^= bits;
    }, 40, 8));
    report("f32_struct_encode_ns", bench_median([&] {
        const auto raw = matrix_writer.encode(matrix, matrix_value);
        sink ^= raw[12];
    }, 40, 8));
    report("f32_struct_decode_ns", bench_median([&] {
        kiln::decode_into(matrix, matrix_writer.bytes(), matrix_slot);
        std::uint32_t bits = 0;
        std::memcpy(&bits, matrix_slot.find("values")->as_f32s().data(), 4);
        sink ^= bits;
    }, 40, 8));
    report("f32_list_encode_ns", bench_median([&] {
        const auto raw = list_writer.encode(f32_list, list_value);
        sink ^= raw[4];
    }, 40, 8));
    kiln::Value list_slot;
    report("f32_list_decode_ns", bench_median([&] {
        kiln::decode_into(f32_list, list_writer.bytes(), list_slot);
        std::uint32_t bits = 0;
        std::memcpy(&bits, list_slot.as_f32s().data(), 4);
        sink ^= bits;
    }, 40, 8));
    report("f32_list_decode_pool_ns", bench_median([&] {
        kiln::Value dropped = kiln::decode(f32_list, list_writer.bytes());
        std::uint32_t bits = 0;
        std::memcpy(&bits, dropped.as_f32s().data(), 4);
        sink ^= bits;
    }, 40, 8));
    report("f32_list_free_encode_ns", bench_median([&] {
        const auto raw = kiln::encode(f32_list, list_value);
        sink ^= raw[4];
    }, 40, 8));
    if (sink == 0xFFFFFFFFu) {
        return 2;
    }
    return 0;
}
