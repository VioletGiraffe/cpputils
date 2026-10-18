#!/bin/sh

# Configures, builds and runs tests/CMakeLists.txt.
# An argument of "debug" builds and runs the debug configuration; the default is release.
# Any further arguments go to the test executable, e.g. a Catch2 test spec such as "[benchmark]".

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG=Release
if [ "${1:-}" = debug ]; then CONFIG=Debug; fi
if [ $# -gt 0 ]; then shift; fi

BUILD_DIR="${ROOT}/build/${CONFIG}"

# CMAKE_BUILD_TYPE serves single-config generators, --config multi-config ones; each ignores the other
cmake -S "${ROOT}/tests" -B "${BUILD_DIR}" -Wno-unused-cli -DCMAKE_BUILD_TYPE="${CONFIG}" -DOUTPUT_DIR="${BUILD_DIR}/bin"
cmake --build "${BUILD_DIR}" --config "${CONFIG}" --parallel "$(getconf _NPROCESSORS_ONLN)"

exec "${BUILD_DIR}/bin/tests" "$@"
