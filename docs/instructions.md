# Instructions

C++20, tested with g++ 11.

```bash
make install PREFIX=/usr/local
```

From a git checkout:

```bash
make test
gld-kilngen-cpp testdata/schema/point.json -o point.hpp
```

Build `gld-kilngen-cpp` with `make` before that command, or install it. The import is `#include "kiln/kiln.hpp"` and the namespace is `kiln`. `encode` and `decode` take a schema and a `Value`. `Writer` reuses one buffer across calls. `schema_from_json` reads the JSON Schema subset: booleans, integers with an optional `format`, numbers, strings, binary strings, arrays, objects, `$ref` of the form `#/$defs/Name`, and `additionalProperties` as a dict. A property omitted from `required` is optional. `gld-kilngen-cpp` writes one header of structs with `encode`, `decode`, and `encoded_size`. A `tensors` field is not produced from that JSON subset. Build a tensor directory with `encode_tensors` and read it with `decode_tensors`.
