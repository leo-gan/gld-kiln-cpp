# gld-kiln-cpp

A from-scratch Kiln codec for C++. The runtime and the code generator are C++. They do not wrap, link, or vendor a C, Python, or Rust Kiln library.

`testdata/reference/kiln_ref.py` is a test oracle for Frozen values. It is not used at run time.

This repository is a standalone library.

Documentation: [Why Kiln](https://leo-gan.github.io/gld-kiln-cpp/why-kiln/),
[Instructions](https://leo-gan.github.io/gld-kiln-cpp/instructions/),
[Examples](https://leo-gan.github.io/gld-kiln-cpp/examples/),
[Techniques](https://leo-gan.github.io/gld-kiln-cpp/techniques/),
[Test data](https://leo-gan.github.io/gld-kiln-cpp/test-data/).

## Install

```bash
make install PREFIX=/usr/local
```

The import is `#include "kiln/kiln.hpp"` in namespace `kiln`. The generator command is `gld-kilngen-cpp`.

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
```

## License

MIT. Copyright (c) 2026 Leonid Ganeline.
