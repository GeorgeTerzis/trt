#!/usr/bin/env bash
set -e

MODE="${1:-debug}"

echo "with mode $MODE" 

cmake -B "build/$MODE" -S . \
    -DCMAKE_BUILD_TYPE="$(tr '[:lower:]' '[:upper:]' <<< ${MODE:0:1})${MODE:1}" \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++
