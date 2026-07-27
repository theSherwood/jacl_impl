;; Scaled mirror of test/jacl/bench_scaled/string_concat.jacl — 400 rounds = 160400.
;; (str s "y") allocates a fresh string each step (O(N^2) per round), matching the
;; JACL rope-concat workload.
(ns bench.string-concat)

(defn- one-round []
  (loop [s "x", i 0]
    (if (< i 400) (recur (str s "y") (inc i)) (count s))))

(defn run []
  (loop [acc 0, i 0]
    (if (< i 400) (recur (+ acc (one-round)) (inc i)) acc)))
