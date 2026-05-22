;; Mirror of test/jacl/bench/box_churn.jacl.
;;
;; Allocates 2000 short-lived single-element atoms (Clojure's closest
;; analog to a JACL box - thread-local mutable container with deref/reset
;; semantics), dereferences each, accumulates the sum. Validates: 1999000.

(ns bench.box-churn)

(defn run []
  (loop [i 0 acc 0]
    (if (< i 2000)
      (let [b (atom i)]
        (recur (inc i) (+ acc @b)))
      acc)))
