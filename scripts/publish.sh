#!/usr/bin/env bash
set -euo pipefail
echo "C++ ships as a GitHub release"
if [[ -z "${KILN_CPP_REGISTRY_TOKEN:-}" ]]; then
  echo "Package registry publish skipped: token is unset"
fi
exit 0
