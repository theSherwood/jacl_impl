;; bench-clojure.clj - runtime perf harness for the Clojure ports of
;; the JACL bench scenarios.
;;
;; Discovers test/clojure/bench/*.clj, loads each as a namespace, runs
;; its run fn with warmup + timed iterations, and emits one JSONL
;; record per scenario on stdout. Output schema mirrors test_perf.c
;; and tools/bench-python.py:
;;
;;   {"scenario": ..., "git_sha": ..., "runtime": "clojure",
;;    "clojure_version": ..., "warmup_iters": N, "timed_iters": N,
;;    "wall_ns_min": ..., "wall_ns_median": ..., "wall_ns_max": ...,
;;    "result": <run() return value>}
;;
;; Env knobs (same names as test_perf.c):
;;   BENCH_WARMUP_ITERS - default 20
;;   BENCH_TIMED_ITERS  - default 60
;;   BENCH_JSON_OUT     - file path; absent => stdout
;;
;; JVM warmup matters more than CPython's: the first ~20 invocations
;; of each `run` are interpreted, then HotSpot compiles. We expose
;; BENCH_WARMUP_ITERS so callers can size warmup appropriately.

(ns bench-clojure
  (:require [clojure.string :as str]
            [clojure.java.shell :as shell])
  (:import [java.io File]))

(def bench-dir-default "test/clojure/bench")

(defn- env-int [name default]
  (let [v (System/getenv name)]
    (if (and v (not (str/blank? v))) (Long/parseLong v) default)))

(defn- git-sha []
  (try
    (let [r (shell/sh "git" "rev-parse" "--short" "HEAD")]
      (or (some-> (:out r) str/trim not-empty) "?"))
    (catch Exception _ "?")))

(defn- ns-for-file [^File f]
  ;; bench/foo_bar.clj => bench.foo-bar
  (let [stem (subs (.getName f) 0 (- (count (.getName f)) 4))]
    (str "bench." (str/replace stem \_ \-))))

(defn- run-one [^File f warmup timed sha]
  (let [scenario (subs (.getName f) 0 (- (count (.getName f)) 4))
        _       (load-file (.getPath f))
        ns-sym  (symbol (ns-for-file f))
        run-fn  (-> (the-ns ns-sym) (ns-resolve 'run))]
    (when-not run-fn
      (throw (ex-info (str (.getName f) " has no run fn") {})))
    ;; Warmup: let HotSpot tier up.
    (dotimes [_ warmup] (run-fn))
    ;; Timed pass.
    (let [timings (long-array timed)
          result  (volatile! nil)]
      (dotimes [i timed]
        (let [t0 (System/nanoTime)
              r  (run-fn)
              dt (- (System/nanoTime) t0)]
          (aset timings i dt)
          (vreset! result r)))
      (java.util.Arrays/sort timings)
      {:scenario scenario
       :git_sha sha
       :runtime "clojure"
       :clojure_version (clojure-version)
       :warmup_iters warmup
       :timed_iters timed
       :wall_ns_min (aget timings 0)
       :wall_ns_median (aget timings (int (quot timed 2)))
       :wall_ns_max (aget timings (dec timed))
       :result @result})))

(defn- escape-json-str [^String s]
  (str "\"" (-> s
                (str/replace "\\" "\\\\")
                (str/replace "\"" "\\\""))
       "\""))

(defn- to-json [v]
  ;; Minimal JSON for our schema: strings, numbers, maps.
  (cond
    (nil? v) "null"
    (true? v) "true"
    (false? v) "false"
    (number? v) (str v)
    (string? v) (escape-json-str v)
    (map? v)
    (str "{"
         (str/join ","
                   (for [[k val] v]
                     (str (escape-json-str (name k)) ":" (to-json val))))
         "}")
    :else (escape-json-str (str v))))

(defn -main [& args]
  (let [bench-dir (or (first args) bench-dir-default)
        warmup    (env-int "BENCH_WARMUP_ITERS" 20)
        timed     (env-int "BENCH_TIMED_ITERS" 60)
        out-path  (System/getenv "BENCH_JSON_OUT")
        sha       (git-sha)
        scripts   (->> (File. ^String bench-dir)
                       .listFiles
                       (filter #(and (.isFile ^File %)
                                     (str/ends-with? (.getName ^File %) ".clj")
                                     (not (str/starts-with? (.getName ^File %) "_"))))
                       (sort-by #(.getName ^File %)))
        out       (if out-path
                    (java.io.PrintWriter. (java.io.FileWriter. ^String out-path))
                    *out*)]
    (binding [*err* *err*]
      (.println System/err
        (format "bench-clojure: %d scenario(s), warmup=%d timed=%d"
                (count scripts) warmup timed)))
    (try
      (doseq [f scripts]
        (try
          (let [rec (run-one f warmup timed sha)
                line (to-json rec)]
            (if out-path
              (do (.println ^java.io.PrintWriter out line)
                  (.flush ^java.io.PrintWriter out))
              (println line))
            (binding [*err* *err*]
              (.println System/err
                (format "  %-24s median=%7.2fms"
                        (:scenario rec)
                        (double (/ (:wall_ns_median rec) 1.0e6))))))
          (catch Throwable t
            (binding [*err* *err*]
              (.println System/err (str "  " (.getName f) ": ERROR " (.getMessage t)))))))
      (finally
        (when out-path (.close ^java.io.PrintWriter out))))
    (shutdown-agents)))

(apply -main *command-line-args*)
