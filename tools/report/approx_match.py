#!/usr/bin/env python3
"""
Approximate static-chain matching for BrInfo.

This module provides a best-effort matcher that, when exact matching fails,
returns the Top-K most similar static chains under the same function.

Inputs:
- meta: loaded meta dict from brinfo_report.load_meta(meta_dir).
- runtime_conds: compressed per-invocation condition events (list of JsonObj),
  where compression has already removed repeated loop iterations and dropped
  the final loop-exit False after entering a loop.

Outputs:
- A list of candidate matches with similarity score and a summarized diff.

Design highlights:
- Path-insensitive semantic ID (sid): sid = f"{cond_kind}\t{cond_norm}".
- Prefilter candidates by sid set Jaccard similarity to limit DP cost.
- Sequence alignment (Needleman-Wunsch-like) over (sid, valEff) pairs.
- Auxiliary LCP/LCS metrics for tie-breaking and explainability.

Note: This module does not modify brinfo_report; integration is expected to
be optional (flags: --approx-match/--approx-topk/--approx-threshold) and
invoke ApproxMatcher.match(...) after exact matching yields zero results.
Matching is restricted to static chains under the same func_hash; if func_hash
is missing or not found in meta, no approximate results are produced.
"""
from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple, Set, Iterable

JsonObj = Dict[str, Any]


# -----------------------
# Utilities and encodings
# -----------------------

def _sid(kind: Optional[str], norm: Optional[str]) -> str:
    k = (kind or "").upper()
    n = norm or ""
    return f"{k}\t{n}"


def _val_eff(ev: JsonObj) -> bool:
    return bool(ev.get("val")) ^ bool(ev.get("norm_flip"))


def _kind_weight(kind: Optional[str]) -> float:
    k = (kind or "").upper()
    if k == "LOOP":
        return 2.0
    if k in ("IF",):
        return 1.0
    if k in ("CASE", "DEFAULT"):
        return 0.5
    return 1.0


# -----------------------
# Static index
# -----------------------

class StaticChain:
    __slots__ = (
        "chain_id",
        "source",
        "seq_hash_val",  # List[Tuple[str, bool]]
        "seq_sid_val",   # List[Tuple[str, bool]]
        "sid_set",       # Set[str]
        "kind_seq",      # List[str]
        "weight_sum",
    )

    def __init__(self, chain_id: int, source: str,
                 seq_hash_val: List[Tuple[str, bool]],
                 seq_sid_val: List[Tuple[str, bool]],
                 kind_seq: List[str]):
        self.chain_id = chain_id
        self.source = source
        self.seq_hash_val = seq_hash_val
        self.seq_sid_val = seq_sid_val
        self.sid_set = {sid for sid, _ in seq_sid_val}
        self.kind_seq = kind_seq
        self.weight_sum = sum(_kind_weight(k) for k in kind_seq)


class StaticIndex:
    def __init__(self):
        # func_hash -> list[StaticChain]
        self.by_func: Dict[str, List[StaticChain]] = {}
        # cond_hash -> (cond_norm, cond_kind)
        self.cond_info_by_hash: Dict[str, Tuple[str, str]] = {}

    @staticmethod
    def from_meta(meta: JsonObj) -> "StaticIndex":
        idx = StaticIndex()
        # Build cond hash -> (norm, kind)
        conds = meta.get("conditions_by_hash") or {}
        for h, info in conds.items():
            if not isinstance(info, dict):
                continue
            cn = info.get("cond_norm") or info.get("norm") or info.get("cond")
            ck = info.get("kind") or info.get("cond_kind")
            if cn is None or ck is None:
                continue
            idx.cond_info_by_hash[str(h)] = (str(cn), str(ck))

        # Build chains
        by_func = meta.get("static_chains_by_func") or {}
        for fh, chains in by_func.items():
            out: List[StaticChain] = []
            if not isinstance(chains, list):
                continue
            chain_id = 0
            for entry in chains:
                try:
                    seq_hash_val, source = entry
                except Exception:
                    continue
                # seq_hash_val should be a list of (cond_hash, value)
                if not isinstance(seq_hash_val, list):
                    continue
                sid_val: List[Tuple[str, bool]] = []
                kinds: List[str] = []
                ok = True
                for t in seq_hash_val:
                    if not isinstance(t, (list, tuple)) or len(t) != 2:
                        ok = False
                        break
                    ch, v = t
                    chs = str(ch)
                    cond = idx.cond_info_by_hash.get(chs)
                    if not cond:
                        # missing condition meta — cannot compute sid, skip this chain
                        ok = False
                        break
                    cn, ck = cond
                    sid_val.append((_sid(ck, cn), bool(v)))
                    kinds.append(ck)
                if not ok:
                    chain_id += 1
                    continue
                out.append(StaticChain(chain_id, str(source), seq_hash_val, sid_val, kinds))
                chain_id += 1
            if out:
                idx.by_func[str(fh)] = out
        return idx


# -----------------------
# Sequence metrics
# -----------------------

def lcp_len(a: List[Tuple[str, bool]], b: List[Tuple[str, bool]]) -> int:
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i


def lcs_len(a: List[Tuple[str, bool]], b: List[Tuple[str, bool]]) -> int:
    n, m = len(a), len(b)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            if a[i] == b[j]:
                dp[i][j] = 1 + dp[i + 1][j + 1]
            else:
                dp[i][j] = max(dp[i + 1][j], dp[i][j + 1])
    return dp[0][0]


# -----------------------
# Alignment and diff
# -----------------------

def align_with_diffs(
    run_sid_val: List[Tuple[str, bool]],
    run_kinds: List[str],
    st_sid_val: List[Tuple[str, bool]],
    st_kinds: List[str],
) -> Tuple[float, List[Dict[str, Any]]]:
    """Needleman-Wunsch-like alignment over (sid, val) with weighted scoring.

    Returns (raw_score, diffs)
    diffs: list of steps, each with op in {keep, flip, subst, ins, del} and payload.
    """
    n, m = len(run_sid_val), len(st_sid_val)
    # DP matrices
    score = [[0.0] * (m + 1) for _ in range(n + 1)]
    prev = [[None] * (m + 1) for _ in range(n + 1)]  # stores (i', j', op)

    def gap_penalty_run(i: int) -> float:
        # delete run[i-1]
        return -0.75 * _kind_weight(run_kinds[i - 1])

    def gap_penalty_st(j: int) -> float:
        # insert st[j-1]
        return -0.75 * _kind_weight(st_kinds[j - 1])

    # init borders
    for i in range(1, n + 1):
        score[i][0] = score[i - 1][0] + gap_penalty_run(i)
        prev[i][0] = (i - 1, 0, "del")
    for j in range(1, m + 1):
        score[0][j] = score[0][j - 1] + gap_penalty_st(j)
        prev[0][j] = (0, j - 1, "ins")

    # fill
    for i in range(1, n + 1):
        sid_i, v_i = run_sid_val[i - 1]
        for j in range(1, m + 1):
            sid_j, v_j = st_sid_val[j - 1]
            w = 0.5 * (_kind_weight(run_kinds[i - 1]) + _kind_weight(st_kinds[j - 1]))
            if sid_i == sid_j:
                s = 2.0 * w if v_i == v_j else -0.5 * w  # keep vs flip
            else:
                s = -1.0 * w  # substitution
            # candidates
            c_match = score[i - 1][j - 1] + s
            c_del = score[i - 1][j] + gap_penalty_run(i)
            c_ins = score[i][j - 1] + gap_penalty_st(j)
            # choose
            if c_match >= c_del and c_match >= c_ins:
                score[i][j] = c_match
                prev[i][j] = (i - 1, j - 1, "match")
            elif c_del >= c_ins:
                score[i][j] = c_del
                prev[i][j] = (i - 1, j, "del")
            else:
                score[i][j] = c_ins
                prev[i][j] = (i, j - 1, "ins")

    # backtrack diffs
    diffs: List[Dict[str, Any]] = []
    i, j = n, m
    while i > 0 or j > 0:
        pi = prev[i][j]
        if pi is None:
            break
        ni, nj, op = pi
        if op == "match":
            sid_i, v_i = run_sid_val[i - 1]
            sid_j, v_j = st_sid_val[j - 1]
            if sid_i == sid_j and v_i == v_j:
                diffs.append({"op": "keep", "run_idx": i - 1, "st_idx": j - 1})
            elif sid_i == sid_j:
                diffs.append({"op": "flip", "run_idx": i - 1, "st_idx": j - 1})
            else:
                diffs.append({"op": "subst", "run_idx": i - 1, "st_idx": j - 1})
        elif op == "del":
            diffs.append({"op": "del", "run_idx": i - 1})
        else:
            diffs.append({"op": "ins", "st_idx": j - 1})
        i, j = ni, nj
    diffs.reverse()
    return score[n][m], diffs


# -----------------------
# Approximate matcher
# -----------------------

class ApproxMatcher:
    def __init__(self, index: StaticIndex):
        self.index = index

    @staticmethod
    def from_meta(meta: JsonObj) -> "ApproxMatcher":
        return ApproxMatcher(StaticIndex.from_meta(meta))

    def _prefilter(self, sid_set_run: Set[str], cand: Iterable[StaticChain], top_m: int = 20) -> List[StaticChain]:
        scored: List[Tuple[float, StaticChain]] = []
        for ch in cand:
            inter = len(sid_set_run & ch.sid_set)
            union = len(sid_set_run | ch.sid_set) or 1
            jacc = inter / union
            scored.append((jacc, ch))
        scored.sort(key=lambda x: x[0], reverse=True)
        return [c for _, c in scored[:top_m]]

    def match(
        self,
        func_hash: Optional[str],
        runtime_conds: List[JsonObj],
        topk: int = 3,
        threshold: float = 0.6,
        prefilter_size: int = 20,
    ) -> List[JsonObj]:
        # Prepare runtime sid/val and kinds
        run_sid_val: List[Tuple[str, bool]] = []
        run_kinds: List[str] = []
        sid_set_run: Set[str] = set()
        for ev in runtime_conds:
            sid = _sid(ev.get("cond_kind"), ev.get("cond_norm"))
            ve = _val_eff(ev)
            run_sid_val.append((sid, ve))
            run_kinds.append(str(ev.get("cond_kind") or ""))
            sid_set_run.add(sid)

        # Collect candidates
        # Restrict to same func_hash only; no global fallback
        if not func_hash or func_hash not in self.index.by_func:
            return []
        cands: List[StaticChain] = list(self.index.by_func[func_hash])
        if not cands:
            return []

        # Prefilter
        cands = self._prefilter(sid_set_run, cands, top_m=prefilter_size)
        if not cands:
            return []

        # Score with alignment
        scored: List[Tuple[float, JsonObj]] = []
        # Precompute run weight sum upper bound
        max_w = sum(_kind_weight(k) for k in run_kinds)
        for ch in cands:
            raw, diffs = align_with_diffs(run_sid_val, run_kinds, ch.seq_sid_val, ch.kind_seq)
            # Normalize score in [0,1] using an optimistic upper bound
            max_possible = max(1e-6, 2.0 * min(max_w, ch.weight_sum))
            norm = max(0.0, min(1.0, raw / max_possible))
            lcp = lcp_len(run_sid_val, ch.seq_sid_val)
            lcs = lcs_len(run_sid_val, ch.seq_sid_val)
            lmin = max(1, min(len(run_sid_val), len(ch.seq_sid_val)))
            lcp_ratio = lcp / lmin
            lcs_ratio = lcs / lmin
            score = 0.7 * norm + 0.2 * lcp_ratio + 0.1 * lcs_ratio
            if score >= threshold:
                scored.append((score, {
                    "source": ch.source,
                    "chain_id": ch.chain_id,
                    "score": round(score, 4),
                    "lcp": lcp,
                    "lcs": lcs,
                    "diffs": diffs,
                }))
        scored.sort(key=lambda x: x[0], reverse=True)
        return [item for _, item in scored[:topk]]
