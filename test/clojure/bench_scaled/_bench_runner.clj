;; Execution-only timing harness for the scaled Clojure mirrors (warmed JVM).
;;
;; Mirrors the methodology of the JACL/Python/Node timers: warm the JIT, then time
;; the hot `run` N times and report the minimum + median in milliseconds. JVM
;; startup and JIT warm-up are deliberately excluded — see bench_scaled/README.md.
;;
;; Run from the repo root:
;;   clojure -M test/clojure/bench_scaled/_bench_runner.clj
(ns bench.runner)

(def scenarios
  ;; name             ns/var                    warmup timed
  [["fib"           'bench.fib/run              5 10]
   ["sieve"         'bench.sieve/run            5 10]
   ["map_lookup"    'bench.map-lookup/run       5 10]
   ["box_churn"     'bench.box-churn/run        5 10]
   ["string_concat" 'bench.string-concat/run    8 15]])

(doseq [[nm] scenarios]
  (load-file (str "test/clojure/bench_scaled/" nm ".clj")))

(defn time-one [nm f warmup timed]
  (dotimes [_ warmup] (f))
  (let [ts (vec (sort (for [_ (range timed)]
                        (let [t0 (System/nanoTime)]
                          (f)
                          (/ (- (System/nanoTime) t0) 1e6)))))]
    (printf "%-15s min_ms=%9.2f med_ms=%9.2f  out=%s%n"
            nm (first ts) (nth ts (quot (count ts) 2)) (str (f)))
    (flush)))

(doseq [[nm sym warm timed] scenarios]
  (time-one nm @(resolve sym) warm timed))
