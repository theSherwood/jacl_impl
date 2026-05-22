;; Mirror of test/jacl/bench/box_literal_churn.jacl.
;;
;; Same shape as box_churn but the boxed value is a literal-arithmetic
;; expression. In JACL this triggers OP_BOX_UNCHECKED (the typer proves
;; the operand cannot carry the error flag); Clojure has no such concept
;; but the bench is run for cross-runtime comparison parity.
;; Validates: 2000 * (2*5) = 20000.

(ns bench.box-literal-churn)

(defn run []
  (loop [i 0 acc 0]
    (if (< i 2000)
      (let [b (atom (* 2 5))]
        (recur (inc i) (+ acc @b)))
      acc)))
