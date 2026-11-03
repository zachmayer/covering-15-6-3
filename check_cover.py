"""Check that cover31.txt is 31 groups of 6 from people 1..15 in which every trio shares a group."""

from itertools import combinations

groups = [frozenset(map(int, line.split())) for line in open("cover31.txt") if line.strip()]
assert len(groups) == 31 and len(set(groups)) == 31
assert all(len(g) == 6 and g <= set(range(1, 16)) for g in groups)
missing = [t for t in combinations(range(1, 16), 3) if not any(set(t) <= g for g in groups)]
assert not missing, missing
print("OK: 31 groups of 6 cover all 455 trios of 15 people")
