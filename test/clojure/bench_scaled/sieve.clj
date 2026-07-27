;; Scaled mirror of test/jacl/bench_scaled/sieve.jacl — primes < 60000 = 6057.
;; Doubly-nested integer loop (trial division). loop/recur is *the* Clojure
;; looping form — this is the idiomatic naive version, matching the Python/JS mirrors.
(ns bench.sieve)

(defn run []
  (let [n 60000]
    (loop [k 2, cnt 0]
      (if (< k n)
        (let [is-p (loop [i 2]
                     (cond (> (* i i) k)     true
                           (zero? (rem k i)) false
                           :else             (recur (inc i))))]
          (recur (inc k) (if is-p (inc cnt) cnt)))
        cnt))))
