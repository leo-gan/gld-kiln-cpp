#!/usr/bin/env bash
set -euo pipefail
if ! command -v g++ >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y g++
fi
g++ --version
