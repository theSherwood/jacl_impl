#!/bin/sh
# Regenerates jacl_value.h from value.usc with unir-schemac, from a unir checkout at $UNIR
# (default ../unir next to this repo). The header is committed, so a runtime build needs no
# Rust toolchain; record the unir commit in UNIR_REV as for the edge unit.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
UNIR=${UNIR:-$DIR/../../../unir}
(cd "$UNIR" && cargo run -q -p unir-schemac -- "$DIR/value.usc" --connection Values \
  --name values --prefix jv_ --c "$DIR/jacl_value.h")
