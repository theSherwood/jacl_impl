"""Idiomatic-Python variant of collection_churn.

Uses mutable dict/list updates instead of functional copy. Not directly
comparable to the JACL scenario (which uses persistent HAMT/RRB), but
provides a "how fast would a Python user write this" reference.
"""


def run() -> int:
    n = 200

    m: dict[int, int] = {}
    for i in range(n):
        m[i] = i
    for i in range(n):
        m[i] = i * 2
    ms = sum(m[i] for i in range(n))

    v: list[int] = []
    for i in range(n):
        v.append(i)
    for i in range(n):
        v[i] = i * 2
    vs = sum(v[i] for i in range(n))

    return ms + vs


if __name__ == "__main__":
    print(run())
