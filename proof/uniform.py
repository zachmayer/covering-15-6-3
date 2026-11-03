"""Uniform minimum-ranked-root cases; one immutable native catalogue per order."""

import ctypes
import hashlib
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from encoding import encode
from linkio import decode
from verify import verify

ROOT = Path(__file__).resolve().parent
SOURCES = ("uniform.cpp", "local_filter.cpp", "uniform.py", "prove_all.py",
           "summarize_all.py", "encoding.py", "linkio.py", "verify.py",
           "check_lrat.cpp", "pyproject.toml", "uv.lock", "prove_all", "summarize")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes() -> dict[str, str]:
    return {name: sha256(ROOT / name) for name in SOURCES}


def ensure_build() -> None:
    work = ROOT / ".work"
    work.mkdir(exist_ok=True)
    inputs = {name: sha256(ROOT / name) for name in ("uniform.cpp", "local_filter.cpp", "check_lrat.cpp")}
    stamp = work / "uniform-build.json"
    expected = {"inputs": inputs, "flags": "-std=c++17 -O3 -Wall -Wextra -Wpedantic"}
    if stamp.exists() and (work / "uniform.so").exists() and (ROOT / "check_lrat").exists():
        actual = {**expected, "native_sha256": sha256(work / "uniform.so"),
                  "checker_sha256": sha256(ROOT / "check_lrat")}
        if json.loads(stamp.read_text()) == actual:
            return
    flags = expected["flags"].split()
    subprocess.run(["c++", *flags, "-fPIC", "-shared", "-o", str(work / "uniform.so"),
                    str(ROOT / "uniform.cpp")], check=True)
    subprocess.run(["c++", *flags, "-o", str(ROOT / "check_lrat"),
                    str(ROOT / "check_lrat.cpp")], check=True)
    stamp.write_text(json.dumps({**expected, "native_sha256": sha256(work / "uniform.so"),
                                "checker_sha256": sha256(ROOT / "check_lrat")}) + "\n")


def read_order(path: Path, count: int) -> list[int]:
    order = list(map(int, path.read_text().split()))
    if sorted(order) != list(range(count)):
        raise ValueError("Order must contain every class exactly once")
    return order


def run_child(command: list[str], output: Path) -> tuple[int, float]:
    """wait4 attributes child CPU to this task, even with two worker threads."""
    with output.open("wb") as stream:
        with subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT) as child:
            _, status, usage = os.wait4(child.pid, 0)
            child.returncode = os.waitstatus_to_exitcode(status)
            return child.returncode, usage.ru_utime + usage.ru_stime


class Uniform:
    def __init__(self, catalogue: Path, order_path: Path, v: int = 15,
                 k: int = 6, b: int = 30) -> None:
        if (v, k, b) not in {(15, 6, 30), (17, 8, 17), (16, 8, 14)}:
            raise ValueError("Unsupported zero-slack parameters")
        ensure_build()
        self.v, self.k, self.b = v, k, b
        self.links = [decode(line, v - 1, k - 1)
                      for line in catalogue.read_bytes().splitlines()]
        if any(len(link) != b * k // v for link in self.links):
            raise ValueError("Wrong replication")
        self.order = read_order(order_path, len(self.links))
        self.ranks = {index: rank for rank, index in enumerate(self.order)}
        self.manifest = {
            "v": v, "k": k, "b": b, "catalogue_classes": len(self.links),
            "catalogue_sha256": sha256(catalogue), "order_sha256": sha256(order_path),
            "scope": "minimum_ranked_root", "sources": source_hashes(),
            "native_sha256": sha256(ROOT / ".work/uniform.so"),
            "checker_sha256": sha256(ROOT / "check_lrat"),
        }
        self.lib = ctypes.CDLL(str(ROOT / ".work/uniform.so"))
        self.lib.uniform_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
        self.lib.uniform_open.restype = ctypes.c_void_p
        self.lib.uniform_close.argtypes = [ctypes.c_void_p]
        self.lib.uniform_close.restype = None
        self.lib.uniform_prepare.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.uniform_prepare.restype = ctypes.c_void_p
        self.lib.uniform_prepare_best.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.uniform_prepare_best.restype = ctypes.c_void_p
        self.lib.uniform_free.argtypes = [ctypes.c_void_p]
        self.lib.uniform_free.restype = None
        self.lib.uniform_last_error.argtypes = []
        self.lib.uniform_last_error.restype = ctypes.c_char_p
        started = time.thread_time()
        self.handle = self.lib.uniform_open(os.fsencode(catalogue), v - 1, k - 1,
                                            os.fsencode(order_path))
        if not self.handle:
            raise RuntimeError(self.lib.uniform_last_error().decode())
        self.build_cpu_seconds = time.thread_time() - started

    def close(self) -> None:
        if self.handle:
            self.lib.uniform_close(self.handle)
            self.handle = None

    def prepare(self, index: int, *, best_point: bool = False) -> dict:
        if self.handle is None:
            raise ValueError("Closed preprocessor")
        method = self.lib.uniform_prepare_best if best_point else self.lib.uniform_prepare
        pointer = method(self.handle, index)
        if not pointer:
            raise RuntimeError(self.lib.uniform_last_error().decode())
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.lib.uniform_free(pointer)

    def solve(self, index: int, solver_limit: int | None = None) -> dict:
        start, start_cpu = time.monotonic(), time.thread_time()
        data = self.prepare(index, best_point=True)
        preparation = time.thread_time() - start_cpu
        star = data["star"]
        cnf, variables = encode(self.v, self.k, self.b, self.links[index], "candidates",
                                data["allowed"], data["incompatible"], star["options"],
                                star["point"] - 1)
        with tempfile.TemporaryDirectory(prefix=f"class-{index:05d}-", dir=ROOT / ".work") as temp:
            work = Path(temp)
            cnf_path, proof_path = work / "case.cnf", work / "case.lrat"
            cnf.to_file(str(cnf_path))
            limits = [] if solver_limit is None else ["-t", str(solver_limit)]
            code, solver_cpu = run_child(
                ["cadical", "--lrat", "--no-binary", *limits, str(cnf_path), str(proof_path)],
                work / "solver.log")
            if code not in (10, 20) and not (solver_limit is not None and code == 0):
                raise RuntimeError(f"CaDiCaL exit {code}: {(work / 'solver.log').read_text()}")
            record = {**self.manifest, "class": index, "rank": self.ranks[index],
                      "status": {10: "SAT", 20: "UNSAT", 0: "TIMEOUT"}[code],
                      "cnf_sha256": sha256(cnf_path), "proof_bytes": proof_path.stat().st_size,
                      "checker_verdict": "NOT_APPLICABLE", "candidates": len(variables),
                      "star_options": len(star["options"]), "star_point": star["point"],
                      "preparation_cpu_seconds": preparation,
                      "solver_cpu_seconds": solver_cpu, "check_cpu_seconds": 0.0}
            if code == 20:
                verdict, check_cpu = run_child(
                    [str(ROOT / "check_lrat"), str(cnf_path), str(proof_path)], work / "check.log")
                if verdict != 0:
                    raise RuntimeError(f"LRAT rejected class {index}: {(work / 'check.log').read_text()}")
                check = json.loads((work / "check.log").read_text())
                if check.get("verified") is not True:
                    raise RuntimeError("Checker did not report verified=true")
                record.update(checker_verdict="CHECKED", check_cpu_seconds=check_cpu)
            elif code == 10:
                true = {int(lit) for line in (work / "solver.log").read_text().splitlines()
                        if line.startswith("v ") for lit in line.split()[1:] if int(lit) > 0}
                blocks = [[0] + [x + 1 for x in range(self.v - 1) if mask >> x & 1]
                          for mask in self.links[index]]
                blocks.extend([[x + 1 for x in range(self.v - 1) if mask >> x & 1]
                               for i, mask in enumerate(variables, 1) if i in true])
                verify(self.v, self.k, self.b, blocks)
                record.update(blocks=blocks, verified=True)
            record["cpu_seconds"] = (time.thread_time() - start_cpu + solver_cpu
                                     + record["check_cpu_seconds"])
            record["seconds"] = time.monotonic() - start
        return record
