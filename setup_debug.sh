#!/usr/bin/env bash
set -e

cmake -B build/debug -S . \
-DCMAKE_BUILD_TYPE=Debug \
-DCMAKE_C_COMPILER=clang \
-DCMAKE_CXX_COMPILER=clang++
