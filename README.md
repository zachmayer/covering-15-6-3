# C(15,6,3) = 31

How few groups of 6, drawn from 15 people, guarantee that every 3 people share a group? **31.** The covering-design tables have listed this as "30 or 31" since the 1990s.

*Mostly AI-generated: the search and this README by Claude Opus 5.5, the proof code by GPT-6 Astra (Codex).*

## 31 is enough

`cover31.txt` lists 31 groups (from the La Jolla Covering Repository):

```sh
python3 check_cover.py   # OK: 31 groups of 6 cover all 455 trios of 15 people
```

## 30 is not

Suppose 30 groups work. Each pair of people is in ≥ 4 groups, since those groups must cover the pair's 13 other people, 4 at a time. So each person is in ≥ ⌈14·4/5⌉ = 12 groups, and 30·6 = 15·12 forces exactly 12.

Take any person and remove them from their 12 groups. What remains is 12 groups of 5 from the other 14 people covering every pair: that person's *shape*. Up to relabelling there are exactly 28,162 shapes, listed in `proof/links_14_5_12.g6`; nauty 2.9.3 regenerates the file byte for byte with `genbg -d5:4 -D5:12 -Y1 12 14 60:60`.

Fix an order on the shapes and pick the person whose shape comes first. For each of the 28,162 shapes, the program shows that no 30-group arrangement has it as the first shape, with no other person's shape earlier. Each case becomes a SAT instance proven unsatisfiable, and the proof is checked. [`proof/PROOF.md`](proof/PROOF.md) gives the argument in one page.

## Check it

Needs `uv`, a C++17 compiler and `cadical` (tested with 3.0.1) on PATH.

```sh
cd proof
./controls                                                 # 10 s: the same engine gets C(17,8,3) = 18 and C(16,8,3) = 14 right
./prove_all links_14_5_12.g6 --jobs 2 --out rerun.jsonl    # about an hour: re-proves all 28,162 cases, prints PROVEN
```

To audit the recorded run without re-running: `gunzip -k results.jsonl.gz && uv sync --frozen && ./summarize results.jsonl`.

**What you trust:**

- That the 28,162 shapes are complete. genbg and an independent generator agree exactly, also with nauty replaced by BLISS.
- The ~1,300 lines of C++ and Python that build each case. 94% of cases are refuted there, before the solver runs: their SAT instance already contains the empty clause.
- The 129-line LRAT checker.

The SAT solver itself is not trusted.

**Cross-checks:** a second search, written independently by a different AI model with its own encoding and symmetry breaking, also refutes every shape. Referees reviewed both implementations, and two five-model panels reviewed the claim; none found a flaw.

## Why it's interesting

- It settles an entry in the covering-design tables that had been open for 30 years.
- AI agents did the work, and also made it believable: two independent implementations, checked proofs, adversarial review, and a full re-check that runs on a laptop.
- The trick that makes it cheap: root at the person whose shape comes first, so later cases have fewer shapes left. It should carry over to other cases where the lower bound is tight.
