-- Scaled mirror of test/jacl/bench_scaled/fib.jacl — fib(34) = 5702887.
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
local function run() return fib(34) end
return { run = run }
