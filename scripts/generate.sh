#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make build/gld-kilngen-cpp
mkdir -p build/generated
./build/gld-kilngen-cpp testdata/schema/point.json -o build/generated/point.hpp
./build/gld-kilngen-cpp testdata/schema/message.json -o build/generated/message.hpp
