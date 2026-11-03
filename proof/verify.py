"""Independent, deliberately small verifier; consumes a solve JSON record."""

import argparse
import itertools
import json
from pathlib import Path


def verify(v: int, k: int, b: int, blocks: list[list[int]]) -> None:
    if len(blocks) != b:
        raise ValueError("Wrong number of blocks")
    universe = set(range(v))
    sets = [set(block) for block in blocks]
    if any(len(block) != k or len(s) != k or not s <= universe
           for block, s in zip(blocks, sets, strict=True)):
        raise ValueError("Invalid block")
    if len({frozenset(s) for s in sets}) != b:
        raise ValueError("Duplicate blocks")
    for triple in itertools.combinations(range(v), 3):
        if not any(set(triple) <= s for s in sets):
            raise ValueError(f"Uncovered triple: {triple}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record")
    args = parser.parse_args()
    record = json.loads(Path(args.record).read_text())
    verify(record["v"], record["k"], record["b"], record["blocks"])
    print("VERIFIED")


if __name__ == "__main__":
    main()
