-- Scaled mirror of test/jacl/bench_scaled/sieve.jacl — primes < 60000 = 6057.
local function run()
  local n, count, k = 60000, 0, 2
  while k < n do
    local is_p, i = 1, 2
    while i * i <= k do
      if k % i == 0 then is_p = 0 end
      i = i + 1
    end
    if is_p == 1 then count = count + 1 end
    k = k + 1
  end
  return count
end
return { run = run }
