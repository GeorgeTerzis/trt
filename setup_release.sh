#!/usr/bin/env bash
set -e

cmake -B build/release -S . \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_C_COMPILER=clang \
-DCMAKE_CXX_COMPILER=clang++

