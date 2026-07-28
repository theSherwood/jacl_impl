#!/usr/bin/env bash
# Cross-codec test: the compiler side (syn_wire, AstNode<->bytes) and the runtime side
# (syn_rt, bytes<->jaclrt plain-data vec) must agree on the wire format. For each
# snippet we do  AST -> wire (gen_wire)  then  wire -> vec -> wire (rt_roundtrip)  and
# assert the bytes are identical. Needs gcc; skips cleanly if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CODEGEN="$(cd "$DIR/../.." && pwd)"
RT="$(cd "$DIR/../../../runtime" && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping rt-codec test."; exit 0; }

"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" "$DIR/gen_wire.c" -lpthread -lm -o "$B/gen_wire"
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$RT" -I "$DIR" "$DIR/rt_roundtrip.c" -lm -o "$B/rt_roundtrip"

snips=("42" "foo" "[+ 1 2]" "[+ 1 [* 2 3]]" "[if [== 1 2] {} { set hit 5 }]" "{ set x 1 }" "[twice [twice 5]]")
fail=0
for s in "${snips[@]}"; do
  "$B/gen_wire" "$s" > "$B/a.bin"
  "$B/rt_roundtrip" < "$B/a.bin" > "$B/b.bin"
  if cmp -s "$B/a.bin" "$B/b.bin"; then echo "  PASS  $s"; else echo "  FAIL  $s (bytes differ)" >&2; fail=1; fi
done
[ $fail = 0 ] && echo "rt-codec: syn_wire and syn_rt agree byte-for-byte." || exit 1
