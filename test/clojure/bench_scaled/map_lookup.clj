;; Scaled mirror of test/jacl/bench_scaled/map_lookup.jacl — 3M lookups = 493733.
;; Persistent hash-map is the idiomatic Clojure analog to Python's dict / JS object.
(ns bench.map-lookup)

(defn run []
  (let [m (into {} (map (fn [i] [i (* i 7)])) (range 200))]
    (loop [j 0, acc 0]
      (if (< j 3000000)
        (let [k (rem (+ (* j 73) 11) 200)]
          (recur (inc j) (rem (+ acc (get m k)) 1000003)))
        acc))))
