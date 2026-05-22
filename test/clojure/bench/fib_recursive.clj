;; Mirror of test/jacl/bench/fib_recursive.jacl.
;;
;; Naive recursive Fibonacci. fib(25) = 75025; ~242 k calls, max stack
;; depth 25. Exercises function-call dispatch (Var deref + invoke) and
;; integer arithmetic.

(ns bench.fib-recursive)

(defn fib [n]
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(defn run [] (fib 25))
