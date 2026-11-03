"""Independent direct encodings of fixed-link extension."""

from itertools import combinations
from math import ceil
from typing import cast

from pysat.card import CardEnc, EncType
from pysat.formula import CNF


class Builder:
    def __init__(self) -> None:
        self.cnf = CNF()

    def var(self) -> int:
        # CNF.__init__ sets integer nv=0; the untyped Formula base declares None.
        self.cnf.nv = cast(int, self.cnf.nv) + 1
        return self.cnf.nv

    def card(self, lits: list[int], low: int, high: int) -> None:
        if low > high or low > len(lits) or high < 0:
            self.cnf.append([])
            return
        if 1 < low == high < len(lits):
            outputs = self.thresholds(lits, high)
            self.cnf.append([outputs[low - 1]])
            self.cnf.append([-outputs[high]])
            return
        if low > 0:
            # IDs are allocated explicitly within this Builder. CNF.extend()
            # also mutates PySAT's unrelated global Formula pool, which is not
            # thread-safe and grows across independent cases. Append locally.
            for clause in CardEnc.atleast(lits, bound=low, top_id=self.cnf.nv,
                                          encoding=EncType.seqcounter).clauses:
                self.cnf.append(clause, update_vpool=False)
        if high < len(lits):
            for clause in CardEnc.atmost(lits, bound=high, top_id=self.cnf.nv,
                                         encoding=EncType.seqcounter).clauses:
                self.cnf.append(clause, update_vpool=False)

    def conjunction(self, lits: list[int]) -> int:
        z = self.var()
        for lit in lits:
            self.cnf.append([-z, lit])
        self.cnf.append([z, *[-lit for lit in lits]])
        return z

    def thresholds(self, lits: list[int], limit: int) -> list[int]:
        """Exact truncated unary count; output j iff at least j+1 inputs hold."""
        if not lits:
            return []
        if len(lits) == 1:
            return lits[:]
        middle = len(lits) // 2
        left = self.thresholds(lits[:middle], limit)
        right = self.thresholds(lits[middle:], limit)
        output = [self.var() for _ in range(min(len(lits), limit + 1))]
        for i in range(len(left) + 1):
            for j in range(len(right) + 1):
                if 0 < i + j <= len(output):
                    self.cnf.append(([-left[i - 1]] if i else [])
                                    + ([-right[j - 1]] if j else [])
                                    + [output[i + j - 1]])
                if i + j < len(output):
                    self.cnf.append(([left[i]] if i < len(left) else [])
                                    + ([right[j]] if j < len(right) else [])
                                    + [-output[i + j]])
        return output

    def ordered(self, a: list[int], b: list[int]) -> None:
        """Lexicographic a <= b, without assuming distinctness."""
        prefix = None
        for x, y in zip(a, b, strict=True):
            condition = [] if prefix is None else [-prefix]
            self.cnf.append([*condition, -x, y])
            eq = self.var()
            self.cnf.append([-eq, -x, y])
            self.cnf.append([-eq, x, -y])
            self.cnf.append([eq, x, y])
            self.cnf.append([eq, -x, -y])
            prefix = eq if prefix is None else self.conjunction([prefix, eq])


def degree_bounds(v: int, k: int, b: int) -> tuple[int, int, int]:
    pair = ceil((v - 2) / (k - 2))
    lower = ceil((v - 1) * pair / (k - 1))
    if (v, k) == (17, 8):
        # C(16,7,2) >= 8: elementary proof in MATH.md and checked LRAT.
        lower = max(lower, 8)
    upper = min(b, b * k - (v - 1) * lower)
    return lower, upper, pair


def encode(v: int, k: int, b: int, link: list[int], mode: str,
           allowed: list[int] | None = None,
           incompatible: list[list[int]] | None = None,
           options: list[list[int]] | None = None,
           option_point: int | None = None,
           selected_option: int | None = None,
           selected_options: list[int] | None = None,
           pair_budget: bool = True,
           maximum_pair: int | None = None) -> tuple[CNF, list]:
    if selected_option is not None and selected_options is not None:
        raise ValueError("Choose one option or a group, not both")
    selection = selected_options if selected_options is not None else (
        [selected_option] if selected_option is not None else None)
    if selection is not None:
        if mode != "candidates" or options is None:
            raise ValueError("Selecting a part requires candidate star options")
        # With no options, part 0 carries the original empty-clause CNF.
        if (not selection or len(set(selection)) != len(selection)
                or any(not 0 <= index < max(1, len(options)) for index in selection)):
            raise ValueError("Part index out of range")
    n = v - 1
    m = b - len(link)
    low, high, pair = degree_bounds(v, k, b)
    degrees = [sum(a >> x & 1 for a in link) for x in range(n)]
    build = Builder()
    if mode == "rows":
        variables = [[build.var() for _ in range(n)] for _ in range(m)]
        for row in variables:
            build.card(row, k, k)
        for a, bb in zip(variables, variables[1:]):
            build.ordered(a, bb)
        for x in range(n):
            build.card([row[x] for row in variables], low - degrees[x], high - degrees[x])
        for size in (2, 3):
            for subset in combinations(range(n), size):
                mask = sum(1 << x for x in subset)
                used = sum(a & mask == mask for a in link)
                needed = max(0, (pair if size == 2 else 1) - used)
                if not needed:
                    continue
                lits = [build.conjunction([row[x] for x in subset]) for row in variables]
                build.card(lits, needed, len(lits))
    elif mode == "candidates":
        variables = ([sum(1 << x for x in s) for s in combinations(range(n), k)]
                     if allowed is None else allowed)
        build.cnf.nv = len(variables)
        if options is not None:
            selectors = []
            support = [[] for _ in variables]
            for option in options:
                selector = build.var()
                selectors.append(selector)
                for variable in option:
                    build.cnf.append([-selector, variable])
                    support[variable - 1].append(selector)
            build.cnf.append(selectors)
            if not options:
                return build.cnf, variables
            if selection is not None:
                build.cnf.append([selectors[index] for index in selection])
                if len(selection) > 1:
                    selected = set(selection)
                    for index, selector in enumerate(selectors):
                        if index not in selected:
                            build.cnf.append([-selector])
            build.card(selectors, 1, 1)
            if option_point is not None:
                for i, mask in enumerate(variables):
                    if mask >> option_point & 1:
                        build.cnf.append([-i - 1, *support[i]])
        for pair_vars in incompatible or []:
            build.cnf.append([-lit for lit in pair_vars])
        if low != high:
            build.card(list(range(1, len(variables) + 1)), m, m)
        for x in range(n):
            lits = [i + 1 for i, a in enumerate(variables) if a >> x & 1]
            build.card(lits, low - degrees[x], high - degrees[x])
        for size in (2, 3):
            for subset in combinations(range(n), size):
                mask = sum(1 << x for x in subset)
                used = sum(a & mask == mask for a in link)
                needed = max(0, (pair if size == 2 else 1) - used)
                if needed:
                    if size == 2 and pair_budget and low == high:
                        continue
                    lits = [i + 1 for i, a in enumerate(variables) if a & mask == mask]
                    build.card(lits, needed, len(lits))
        if pair_budget and low == high:
            budget = low * (k - 1) - n * pair
            excess = [[] for _ in range(n)]
            fixed_excess = [degree - pair for degree in degrees]
            for x, y in combinations(range(n), 2):
                mask = (1 << x) | (1 << y)
                used = sum(a & mask == mask for a in link)
                baseline = pair - used
                lits = [i + 1 for i, a in enumerate(variables) if a & mask == mask]
                # The pair's excess cannot exceed either endpoint's budget.
                cap = min(budget - (degrees[x] - pair), budget - (degrees[y] - pair))
                if maximum_pair is not None:
                    cap = min(cap, maximum_pair - pair)
                outputs = build.thresholds(lits, max(0, baseline + cap))
                if baseline > len(outputs):
                    build.cnf.append([])
                elif baseline > 0:
                    build.cnf.append([outputs[baseline - 1]])
                if baseline + cap < 0:
                    build.cnf.append([])
                elif baseline + cap < len(outputs):
                    build.cnf.append([-outputs[baseline + cap]])
                for point in (x, y):
                    excess[point].extend(outputs[max(0, baseline):max(0, baseline + cap)])
                    # Fixed blocks may already exceed the pair baseline.
                    if baseline < 0:
                        fixed_excess[point] += -baseline
            for point in range(n):
                build.card(excess[point], 0, budget - fixed_excess[point])
    else:
        raise ValueError(mode)
    return build.cnf, variables
