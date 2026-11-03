"""Exhaustive, resumable, uniformly ordered proof run. Never counts unchecked UNSAT."""

import argparse
import fcntl
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from uniform import ROOT, Uniform, sha256, source_hashes


def append(stream, record: dict) -> None:
    stream.write(json.dumps(record, separators=(",", ":")) + "\n")
    stream.flush()
    os.fsync(stream.fileno())


def run(catalogue: Path, order: Path, output: Path, jobs: int = 1,
        parameters: tuple[int, int, int] = (15, 6, 30),
        indices: list[int] | None = None) -> dict:
    if jobs not in (1, 2):
        raise ValueError("Use --jobs 1 or 2: the coordinator plus at most two children")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("a+") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        # A crash during write may leave a partial final line. It is never a result.
        stream.seek(0)
        previous = stream.read()
        if previous and not previous.endswith("\n"):
            stream.seek(0)
            stream.truncate(previous.rfind("\n") + 1)
            stream.flush()
            os.fsync(stream.fileno())
        engine = Uniform(catalogue, order, *parameters)
        try:
            stream.seek(0)
            done = set()
            for line in stream:
                record = json.loads(line)
                if any(record.get(key) != value for key, value in engine.manifest.items()):
                    raise ValueError("Resume refused: catalogue, order, code or executable changed")
                index = record["class"]
                if index in done or record["rank"] != engine.ranks[index]:
                    raise ValueError("Duplicate class or wrong rank in results")
                if record["status"] == "UNSAT" and record["checker_verdict"] == "CHECKED":
                    done.add(index)
                elif record["status"] == "SAT" and record.get("verified") is True:
                    done.add(index)
                else:
                    raise ValueError("Unfinished or invalid record; preserve it and use a new output")
            wanted = engine.order if indices is None else indices
            pending = [index for index in wanted if index not in done]
            print(json.dumps({"order_sha256": engine.manifest["order_sha256"],
                              "build_cpu_seconds": engine.build_cpu_seconds,
                              "pending": len(pending), "jobs": jobs}), file=sys.stderr, flush=True)
            # executor.map preserves order. At most jobs solver/checker children exist.
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                for number, record in enumerate(pool.map(engine.solve, pending, buffersize=jobs), 1):
                    if source_hashes() != engine.manifest["sources"]:
                        raise ValueError("Source changed during run")
                    append(stream, record)
                    if number % 100 == 0 or record["status"] != "UNSAT":
                        print(json.dumps({"completed": number, "class": record["class"],
                                          "status": record["status"]}), file=sys.stderr, flush=True)
            return {"new_classes": len(pending), "build_cpu_seconds": engine.build_cpu_seconds,
                    "order_sha256": sha256(order)}
        finally:
            engine.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("links", type=Path)
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1)
    parser.add_argument("--out", type=Path, default=Path("results.jsonl"))
    args = parser.parse_args()
    order = ROOT / "order.txt"
    expected = (ROOT / "order.sha256").read_text().split()[0]
    if sha256(order) != expected:
        raise ValueError("Pinned order SHA-256 mismatch")
    if len(args.links.read_bytes().splitlines()) != 28162:
        raise ValueError("Target catalogue must contain exactly 28162 classes")
    run(args.links.resolve(), order, args.out.resolve(), args.jobs)
    from_summary(args.out)


def from_summary(output: Path) -> None:
    command = [str(ROOT / "summarize"), str(output)]
    os.execv(command[0], command)


if __name__ == "__main__":
    main()
