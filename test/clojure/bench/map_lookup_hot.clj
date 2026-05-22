;; Mirror of test/jacl/bench/map_lookup_hot.jacl.
;;
;; Builds a 200-entry persistent hash-map (i -> i*7), then 10 k lookups
;; against the deterministic scattered key sequence ((j*73 + 11) mod 200).
;; Targets the persistent-map read hot path - directly comparable to
;; JACL's HAMT-get.
;; Validates: 6_965_000.

(ns bench.map-lookup-hot)

(defn run []
  (let [m (persistent!
            (reduce (fn [tm i] (assoc! tm i (* i 7)))
                    (transient {}) (range 200)))]
    (loop [j 0 acc 0]
      (if (< j 10000)
        (let [k (mod (+ (* j 73) 11) 200)]
          (recur (inc j) (+ acc (get m k))))
        acc))))
