#!/usr/bin/env bash
set -e

cmake --build build/debug -j$(nproc)
