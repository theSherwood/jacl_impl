;; Mirror of test/jacl/bench/spawn_chain.jacl.
;;
;; 50 sequential spawn+await pairs. JVM futures via `future` + `@`
;; dispatch onto the agent thread pool; closest semantic to JACL's
;; `spawn` (submit + await). Each future does the same trivial work
;; (i + 1); the cost is dominated by future allocation, task scheduling,
;; completion handoff, and result deref - mirroring spawn_chain's
;; intent of measuring spawn latency rather than throughput.
;; Validates: sum 1..50 = 1275.

(ns bench.spawn-chain)

(defn run []
  (loop [i 0 acc 0]
    (if (< i 50)
      (let [f (future (+ i 1))]
        (recur (inc i) (+ acc @f)))
      acc)))
