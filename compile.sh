#!/usr/bin/env bash
set -e

MODE="${1:-debug}"
echo "with mode $MODE" 

cmake --build "build/$MODE" -j"$(nproc)"
