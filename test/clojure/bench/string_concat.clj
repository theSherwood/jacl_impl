;; Mirror of test/jacl/bench/string_concat.jacl.
;;
;; 500 concatenations of "y" onto an accumulating string. Clojure
;; strings are JVM Strings - immutable, so `str` allocates a fresh
;; String each time and copies. No equivalent of CPython's refcount==1
;; in-place realloc fast path. This makes Clojure the honest O(N^2)
;; reference for what JACL's rope-based concat is doing.
;;
;; A real Clojure program would use `clojure.string/join` over a vec
;; or a StringBuilder; that's the moral equivalent of recommending a
;; "string builder" pattern in JACL.
;; Validates: 501.

(ns bench.string-concat)

(defn run []
  (loop [i 0 s "x"]
    (if (< i 500)
      (recur (inc i) (str s "y"))
      (count s))))
