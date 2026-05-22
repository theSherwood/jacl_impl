;; Mirror of test/jacl/bench/parallel_map_reduce.jacl.
;;
;; 4-way parallel sum of integer ranges. Uses Clojure futures (deref-
;; ed via @) which run on the agent thread pool - closest analog to
;; JACL's `parallel` primitive (submit N tasks, await all). Unlike
;; Python's ThreadPoolExecutor + GIL, JVM futures get real OS-thread
;; parallelism, so this is the fairest parallel-throughput comparison
;; in the suite.
;; Validates: sum 0..9999 = 49_995_000.

(ns bench.parallel-map-reduce)

(defn sum-range [lo hi]
  (loop [i lo s 0]
    (if (< i hi) (recur (inc i) (+ s i)) s)))

(defn run []
  (let [f1 (future (sum-range 0 2500))
        f2 (future (sum-range 2500 5000))
        f3 (future (sum-range 5000 7500))
        f4 (future (sum-range 7500 10000))]
    (+ @f1 @f2 @f3 @f4)))
