-- Scaled mirror of test/jacl/bench_scaled/map_lookup.jacl — 3M lookups = 493733.
local function run()
  local m = {}
  for i = 0, 199 do m[i] = i * 7 end
  local acc = 0
  for j = 0, 2999999 do
    local k = (j * 73 + 11) % 200
    acc = (acc + m[k]) % 1000003
  end
  return acc
end
return { run = run }
