#!/usr/bin/env python3
from typing import Dict, Any, List, Optional, Tuple, Set

JsonObj = Dict[str, Any]


def effective_val(ev: JsonObj) -> bool:
    return bool(ev.get('val')) ^ bool(ev.get('norm_flip'))


def compress_loop_iterations(conds: List[JsonObj]) -> List[JsonObj]:
    """Compress repeated loop iterations; drop final exit False if loop entered.
    Raw val is used to decide loop entry/exit, effective_val is not used here.
    """
    n = len(conds)
    if n <= 1:
        return list(conds)
    out: List[JsonObj] = []
    i = 0

    def is_loop(ev: JsonObj) -> bool:
        return (ev.get('cond_kind') or '').upper() == 'LOOP'

    while i < n:
        ev = conds[i]
        if not is_loop(ev):
            out.append(ev)
            i += 1
            continue
        loop_hash = ev.get('cond_hash')
        # raw enter
        if not bool(ev.get('val')):
            out.append(ev)
            i += 1
            continue
        # keep first True
        out.append(ev)
        # find next loop head occurrence to delimit body
        j = i + 1
        while j < n:
            ej = conds[j]
            if is_loop(ej) and ej.get('cond_hash') == loop_hash:
                break
            j += 1
        if j > i + 1:
            out.extend(compress_loop_iterations(conds[i + 1:j]))
        # skip to after last raw False for this loop head, but do not keep it
        k: Optional[int] = None
        p = j
        while p < n:
            ep = conds[p]
            if is_loop(ep) and ep.get('cond_hash') == loop_hash and not bool(ep.get('val')):
                k = p
            p += 1
        i = (k + 1) if k is not None else j
    return out
