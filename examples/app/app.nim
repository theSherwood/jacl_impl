## An application's own Nim beside a JACL program (theSherwood/jacl_impl#290). Its `main` runs the
## JACL program, whose `extern`s call `app_scale` back.

proc jaclEntry(): int64 {.importc: "__jacl_entry".}

proc appScale(x: int64; p: pointer; n: int64): int64 {.exportc: "app_scale".} =
  ## `x` times the sum of the `n` bytes at `p`: 64-bit integers, and a pointer into a JACL buffer.
  let bytes = cast[ptr UncheckedArray[uint8]](p)
  var sum = 0'i64
  for i in 0 ..< int(n):
    sum = sum + int64(bytes[i])
  result = x * sum

discard jaclEntry()
