#!/bin/sh
# Produce the JACL runtime LLVM-bitcode artifact (the clang stage of the pipeline:
# runtime C -> clang -O2 -emit-llvm). Translation to SVM IR (svm-llvm) + static
# linking against a program module are exercised in-process by runtime/harness; a
# standalone svm-llvm-translate CLI is the Phase-2 piece (tied to the name->index
# export ask). Requires clang (LLVM 18 to match the svm submodule).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${DIR}/build"
mkdir -p "$OUT"
clang -O2 -emit-llvm -c -DNDEBUG -fno-vectorize -fno-slp-vectorize \
  -I "$DIR" "$DIR/jaclrt.c" -o "$OUT/jaclrt.bc"
echo "runtime artifact: $OUT/jaclrt.bc"
