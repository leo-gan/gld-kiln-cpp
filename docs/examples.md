# Examples

`Point` has two `i32` fields. Kiln writes them in that order. Eight little-endian bytes, and no names.

```cpp
#include "kiln/kiln.hpp"

kiln::Schema point = kiln::struct_of({
    {"x", kiln::scalar(kiln::Kind::I32)},
    {"y", kiln::scalar(kiln::Kind::I32)},
});
kiln::Writer writer(64);
auto raw = writer.encode(point, kiln::Value::dict({
    {"x", kiln::Value::i64(9)},
    {"y", kiln::Value::i64(-4)},
}));
kiln::Value back = kiln::decode(point, raw);
```

`back` is the struct value and `raw.size()` is 8.

A tensor directory keeps the numeric payload as the array. The view's `data` field is a slice of the input buffer.

```cpp
#include "kiln/kiln.hpp"

float samples[2] = {1.0f, -2.0f};
kiln::Tensor tensor;
tensor.dtype = kiln::dtype_from_name("f32");
kiln::set_shape(tensor, {2});
tensor.data = kiln::ByteView(reinterpret_cast<const std::uint8_t*>(samples), sizeof(samples));
std::vector<std::uint8_t> block = kiln::encode_tensors({tensor});
kiln::TensorDirectory directory = kiln::decode_tensors(block);
```

`examples/roundtrip.cpp` is the same point round-trip as a small program.
