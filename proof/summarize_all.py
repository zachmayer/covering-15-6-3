"""Fail closed unless this code/order accounts for exactly all 28162 classes."""

import argparse
import json
import sys
from pathlib import Path

from uniform import ROOT, read_order, sha256, source_hashes


def audit(path: Path) -> dict:
    order = read_order(ROOT / "order.txt", 28162)
    order_digest = sha256(ROOT / "order.txt")
    if (ROOT / "order.sha256").read_text().split()[0] != order_digest:
        raise ValueError("Pinned order digest mismatch")
    records = [json.loads(line) for line in path.read_text().splitlines()]
    if len(records) != 28162 or {r["class"] for r in records} != set(range(28162)):
        raise ValueError(f"Need exactly 28162 distinct classes, found {len(records)} records")
    sources = source_hashes()
    expected_catalogue = "ac1bf6fc1fabd27a82ebd7f11bfd42c7add2c6466a313004653e89094d4103bc"
    for record in records:
        if (record["status"] != "UNSAT" or record["checker_verdict"] != "CHECKED"
                or record["scope"] != "minimum_ranked_root"
                or record["order_sha256"] != order_digest
                or record["catalogue_sha256"] != expected_catalogue
                or record["catalogue_classes"] != 28162
                or (record["v"], record["k"], record["b"]) != (15, 6, 30)
                or record["sources"] != sources
                or type(record["rank"]) is not int or not 0 <= record["rank"] < 28162
                or order[record["rank"]] != record["class"]
                or type(record["proof_bytes"]) is not int or record["proof_bytes"] < 0
                or len(record["cnf_sha256"]) != 64
                or any(c not in "0123456789abcdef" for c in record["cnf_sha256"])):
            raise ValueError(f"Invalid or mismatched record for class {record.get('class')}")
    for field in ("native_sha256", "checker_sha256"):
        if len({record[field] for record in records}) != 1:
            raise ValueError(f"Mixed executable versions: {field}")
    return {"status": "PROVEN", "classes": len(records), "order_sha256": order_digest,
            "class_core_hours": sum(r["cpu_seconds"] for r in records) / 3600,
            "proof_bytes": sum(r["proof_bytes"] for r in records)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.results)
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f"NOT PROVEN: {error}")
        sys.exit(1)
    print("PROVEN")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
