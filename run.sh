#!/usr/bin/env bash
set -e

MODE="${1:-debug}"
echo "with mode $MODE" 

"./build/$MODE/trt" ./examples/file3.trt
