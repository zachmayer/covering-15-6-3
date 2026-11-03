"""Minimal graph6 decoder for the supplied bipartite link catalogues."""

from pathlib import Path


def decode(line: bytes, n: int, k: int) -> list[int]:
    line = line.strip().removeprefix(b">>graph6<<")
    if not line or not 63 <= line[0] < 126:
        raise ValueError("Only graph6 orders below 63 are supported")
    order = line[0] - 63
    r = order - n
    if r <= 0 or len(line) != 1 + (order * (order - 1) // 2 + 5) // 6:
        raise ValueError("Invalid graph6 order or length")
    data = [c - 63 for c in line[1:]]
    if any(c < 0 or c > 63 for c in data):
        raise ValueError("Invalid graph6 byte")
    blocks = [0] * r
    bit = 0
    for j in range(1, order):
        for i in range(j):
            edge = (data[bit // 6] >> (5 - bit % 6)) & 1
            bit += 1
            if not edge:
                continue
            if not i < r <= j:
                raise ValueError("Graph is not in the prescribed bipartition")
            blocks[i] |= 1 << (j - r)
    if any(b.bit_count() != k for b in blocks):
        raise ValueError("Wrong link block size")
    if len(set(blocks)) != r:
        raise ValueError("Repeated link blocks")
    if any(not any(b & (1 << x) and b & (1 << y) for b in blocks)
           for x in range(n) for y in range(x + 1, n)):
        raise ValueError("Link does not cover every pair")
    return blocks


def read_link(path: str, index: int, n: int, k: int) -> list[int]:
    if index < 0:
        raise ValueError("Class indices are zero-based and nonnegative")
    with Path(path).open("rb") as stream:
        for i, line in enumerate(stream):
            if i == index:
                return decode(line, n, k)
    raise ValueError("Class index out of range")
