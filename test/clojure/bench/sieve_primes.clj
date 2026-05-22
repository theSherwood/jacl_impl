;; Mirror of test/jacl/bench/sieve_primes.jacl.
;;
;; Count primes below N=2000 via trial division. Pure arithmetic, no
;; allocation. Validates: 303.

(ns bench.sieve-primes)

(defn ^boolean prime? [^long n]
  (loop [i 2]
    (cond
      (> (* i i) n) true
      (zero? (rem n i)) false
      :else (recur (inc i)))))

(defn run []
  (loop [k 2 count 0]
    (if (< k 2000)
      (recur (inc k) (if (prime? k) (inc count) count))
      count)))
