#!/usr/bin/env bash
set -e

MODE="${1:-release}"
echo "mode $MODE" 

"./build/$MODE/trt" ./examples/file.trt
