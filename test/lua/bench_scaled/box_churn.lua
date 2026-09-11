-- Scaled mirror of test/jacl/bench_scaled/box_churn.jacl — 3M small allocs = 21000000.
-- The 1-element table is the closest Lua analog to JACL's `[box 7]`.
local function run()
  local acc = 0
  for _ = 1, 3000000 do
    local b = { 7 }
    acc = acc + b[1]
  end
  return acc
end
return { run = run }
