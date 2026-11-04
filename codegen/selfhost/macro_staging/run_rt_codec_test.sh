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

# The wide entries are jacl #113: the wire carries AST_LIT_INT as 8 bytes (since SYNW_VERSION 2),
# and the runtime side rebuilds it through jacl_i64_box so the plain-data form stays
# canonical — an inline i32 where one fits, a boxed i64 only where it must. INT64_MIN and
# INT64_MAX are here because they are where a sign-extension slip shows up.
# The rest are every other node kind a macro argument can hold (wire v3): interpolated
# strings, spreads, shell commands (with `&`), break/continue/return, and the quote forms.
snips=("42" "foo" "[+ 1 2]" "[+ 1 [* 2 3]]" "[if [== 1 2] {} { set hit 5 }]" "{ set x 1 }" \
       "[twice [twice 5]]" "5000000000" "[+ 5000000000 1]" "9223372036854775807" \
       "[- 0 9223372036854775807]" "[+ 5000000000 [* 3000000000 2]]" \
       "\"x is \$x and \$[+ 1 2]\"" "{ print \"x is \$x\" }" "[vec ..\$xs]" \
       "{ !ls -la | !wc -l }" "{ !sleep 1 & }" \
       "{ for \$xs x { if [> \$x 2] { break } else { continue } } }" \
       "{ return 5 }" "{ return }" "{ break 3 }" \
       "syntax-quote [if ~c {} ~@body]" "quote [a b]")
fail=0
for s in "${snips[@]}"; do
  "$B/gen_wire" "$s" > "$B/a.bin"
  "$B/rt_roundtrip" < "$B/a.bin" > "$B/b.bin"
  if cmp -s "$B/a.bin" "$B/b.bin"; then echo "  PASS  $s"; else echo "  FAIL  $s (bytes differ)" >&2; fail=1; fi
done
[ $fail = 0 ] || exit 1
echo "rt-codec: syn_wire and syn_rt agree byte-for-byte."

# `syntax-str` (syn_rt's printer, run inside a staged macro) must write what the frontend's
# ast_pretty_print writes for the same node: both sets of snippets, plus the printer's own
# cases — floats (printf %g, then `.0`), strings that need quoting and escaping, `->`, procs and
# lambdas, `^name`.
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$RT" -I "$DIR" "$DIR/rt_syntax_str.c" -lm -o "$B/rt_syntax_str"
pp_snips=("3.14" "0.5" "100.0" "2.5" "0.0001" "0.00001" "123456.0" "1234567.0" "0.1" "-2.75" \
          "-7" "\"hello world\"" "\"\"" "\"a\\\\b\"" "\"say \\\"hi\\\"\"" "\"x\\ny\"" "\"7up\"" "\$p->x" \
          "proc f {a b} { + \$a \$b }" "[\\ + \$it 1]" "syntax-quote [def ^it 1]" \
          "[- 0 9223372036854775807]")
for s in "${snips[@]}" "${pp_snips[@]}"; do
  want="$("$B/gen_wire" --pp "$s")"
  got="$("$B/gen_wire" "$s" | "$B/rt_syntax_str")" || got="<syntax-str failed>"
  if [ "$got" = "$want" ]; then echo "  PASS  syntax-str $s  ->  $got"
  else echo "  FAIL  syntax-str $s" >&2; echo "    got:  $got" >&2; echo "    want: $want" >&2; fail=1; fi
done
[ $fail = 0 ] && echo "rt-codec: syntax-str agrees with the frontend's pretty-printer." || exit 1
