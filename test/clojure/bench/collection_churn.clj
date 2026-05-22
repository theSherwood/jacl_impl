;; Mirror of test/jacl/bench/collection_churn.jacl.
;;
;; Builds a 200-entry map and a 200-element vector, rebuilds each with
;; new values (functional/persistent updates), then sums all values
;; back via 200 reads each. Clojure's persistent collections are the
;; closest semantic match to JACL's HAMT/RRB, which makes this the
;; fairest cross-runtime comparison in the suite.
;; Validates: 2 * (sum 0..199 of i*2) = 2 * 39800 = 79600.

(ns bench.collection-churn)

(defn run []
  (let [n 200
        m1 (reduce (fn [m i] (assoc m i i)) {} (range n))
        m2 (reduce (fn [m i] (assoc m i (* i 2))) m1 (range n))
        ms (reduce + (map #(get m2 %) (range n)))
        v1 (reduce conj [] (range n))
        v2 (reduce (fn [v i] (assoc v i (* i 2))) v1 (range n))
        vs (reduce + (map #(nth v2 %) (range n)))]
    (+ ms vs)))
