;; Scaled mirror of test/jacl/bench_scaled/box_churn.jacl — 3M small allocs = 21000000.
;; (vector 7) is the closest JVM analog to JACL's [box 7]: a fresh single-slot
;; container each iteration. Note the JVM's escape analysis can scalar-replace this
;; short-lived allocation entirely — see README.
(ns bench.box-churn)

(defn run []
  (loop [i 0, acc 0]
    (if (< i 3000000)
      (let [b (vector 7)]
        (recur (inc i) (+ acc (nth b 0))))
      acc)))
