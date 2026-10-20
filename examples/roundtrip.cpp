#include "kiln/kiln.hpp"

#include <iostream>

int main() {
    const kiln::Schema point = kiln::struct_of({
        {"x", kiln::scalar(kiln::Kind::I32)},
        {"y", kiln::scalar(kiln::Kind::I32)},
    });
    kiln::Writer writer(64);
    const auto raw = writer.encode(point, kiln::Value::dict({
                                              {"x", kiln::Value::i64(9)},
                                              {"y", kiln::Value::i64(-4)},
                                          }));
    const kiln::Value back = kiln::decode(point, raw);
    std::cout << back.find("x")->as_i64() << " " << back.find("y")->as_i64() << " " << raw.size() << "\n";
    return 0;
}
