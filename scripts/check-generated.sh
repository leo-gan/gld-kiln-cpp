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
./build/gld-kilngen-cpp testdata/schema/series.json -o "$tmpdir/series.hpp"
./build/gld-kilngen-cpp testdata/schema/series.json -o "$tmpdir/series2.hpp"
cmp "$tmpdir/point.hpp" "$tmpdir/point2.hpp"
cmp "$tmpdir/message.hpp" "$tmpdir/message2.hpp"
cmp "$tmpdir/series.hpp" "$tmpdir/series2.hpp"
cat > "$tmpdir/main.cpp" << 'EOF'
#include "point.hpp"
#include "message.hpp"
#include "series.hpp"

#include <cstdint>
#include <cstring>
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
    kiln::Series series;
    std::uint32_t neg = 0x80000000u;
    float neg_f = 0;
    std::memcpy(&neg_f, &neg, 4);
    series.f32s = {neg_f, 1.0f};
    series.f64s = {-0.0, 2.5};
    series.i32s = {-3};
    series.i64s = {9};
    series.u64s = {4};
    const kiln::Series round = kiln::Series::decode(series.encode());
    std::uint32_t got = 0;
    std::memcpy(&got, round.f32s.data(), 4);
    if (got != neg || round.f32s.size() != 2 || round.f32s[1] != 1.0f) return 1;
    if (round.f64s.size() != 2 || round.i32s.size() != 1 || round.i32s[0] != -3) return 1;
    if (round.i64s.size() != 1 || round.i64s[0] != 9 || round.u64s.size() != 1 || round.u64s[0] != 4) return 1;
    return 0;
}
EOF
g++ -std=c++20 -Wall -Wextra -Werror -O2 -Iinclude -I"$tmpdir" "$tmpdir/main.cpp" build/libkiln.a -o "$tmpdir/main"
"$tmpdir/main"
