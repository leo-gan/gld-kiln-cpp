#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make build/libkiln.a build/gld-kilngen-cpp
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
./build/gld-kilngen-cpp testdata/schema/point.json -o "$tmpdir/point.hpp"
./build/gld-kilngen-cpp testdata/schema/point.json -o "$tmpdir/point2.hpp"
./build/gld-kilngen-cpp testdata/schema/message.json -o "$tmpdir/message.hpp"
./build/gld-kilngen-cpp testdata/schema/message.json -o "$tmpdir/message2.hpp"
cmp "$tmpdir/point.hpp" "$tmpdir/point2.hpp"
cmp "$tmpdir/message.hpp" "$tmpdir/message2.hpp"
cat > "$tmpdir/main.cpp" << 'EOF'
#include "point.hpp"
#include "message.hpp"
int main() {
    kiln::Point point;
    point.x = 3;
    point.y = -1;
    point.label = "n";
    if (kiln::Point::decode(point.encode()).x != 3) return 1;
    if (kiln::Point::encoded_size(point) != point.encode().size()) return 1;
    kiln::Message message;
    message.f_bool = true;
    message.f_string = "hello";
    message.meta.region = "us";
    message.meta.flag = true;
    const kiln::Message back = kiln::Message::decode(message.encode());
    if (back.f_string != "hello" || back.meta.region != "us" || !back.meta.flag) return 1;
    return 0;
}
EOF
g++ -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude -I"$tmpdir" "$tmpdir/main.cpp" build/libkiln.a -o "$tmpdir/main"
"$tmpdir/main"
