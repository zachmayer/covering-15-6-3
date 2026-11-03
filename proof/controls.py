"""Run the same engine on two known cases: it must refute every C(17,8,3) class (C(17,8,3) = 18) and find a
verified 14-block covering for C(16,8,3) = 14. Catalogues: genbg -d7:3 -D7:8 -Y1 8 16 56:56 and
genbg -d7:3 -D7:7 -Y1 7 15 49:49."""

import json

from uniform import ROOT, Uniform, ensure_build


def run(catalogue: str, v: int, k: int, b: int) -> list[dict]:
    lines = (ROOT / catalogue).read_bytes().splitlines()
    order = ROOT / ".work" / f"order-{catalogue}.txt"
    order.write_text("".join(f"{i}\n" for i in range(len(lines))))
    engine = Uniform(ROOT / catalogue, order, v, k, b)
    try:
        return [engine.solve(i) for i in engine.order]
    finally:
        engine.close()


def main() -> None:
    ensure_build()
    negative = run("links_16_7_8.g6", 17, 8, 17)
    assert len(negative) == 185 and all(r["status"] == "UNSAT" and r["checker_verdict"] == "CHECKED" for r in negative)
    print("C(17,8,3): all 185 classes refuted, proofs checked (correct: C(17,8,3) = 18)")
    positive = [r for r in run("links_15_7_7.g6", 16, 8, 14) if r["status"] == "SAT"]
    assert positive and all(r["verified"] for r in positive)
    print(f"C(16,8,3): found and verified a 14-block covering (correct: C(16,8,3) = 14): {json.dumps(positive[0]['blocks'])}")


if __name__ == "__main__":
    main()
