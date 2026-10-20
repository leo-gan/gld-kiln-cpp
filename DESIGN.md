# Kiln for C++

Kiln is a positional little-endian codec. The caller passes the schema. `Writer` owns a buffer and reuses it. `decode` is checked. A tensor payload is a `ByteView` into the input.

The normative text is [Kiln wire v2](https://leo-gan.github.io/gld-kiln/spec/). This repo does not copy that text into a Spec page.

| Topic | Choice |
| --- | --- |
| Wire | Frozen layout and the version-2 tensor directory |
| API | `encode` / `decode`, `Writer`, `encode_tensors` / `decode_tensors`, `schema_from_json`, `gld-kilngen-cpp` |
| Lengths | `u32` byte counts. Dict keys are sorted. Default cap 16777216 |
| Errors | `Error::code()` is FRAME, LIMIT, EOF, TYPE, UTF8, TRAILING, or SCHEMA |
| Package | namespace `kiln`, C++20, g++ 11, MIT |
