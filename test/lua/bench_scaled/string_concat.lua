-- Scaled mirror of test/jacl/bench_scaled/string_concat.jacl — 400 rounds = 160400.
-- `s = s .. "y"` allocates a fresh string each step (O(N^2) per round).
local function one_round()
  local s = "x"
  for _ = 1, 400 do s = s .. "y" end
  return #s
end
local function run()
  local acc = 0
  for _ = 1, 400 do acc = acc + one_round() end
  return acc
end
return { run = run }
