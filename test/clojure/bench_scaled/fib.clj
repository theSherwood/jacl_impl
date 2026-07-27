;; Scaled mirror of test/jacl/bench_scaled/fib.jacl — fib(34) = 5702887.
;; Naive recursion: ~11.4M calls, exercises call dispatch + integer arithmetic.
(ns bench.fib)

(defn fib [n]
  (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(defn run [] (fib 34))
