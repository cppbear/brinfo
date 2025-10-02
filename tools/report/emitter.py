#!/usr/bin/env python3
from typing import Dict, Any, List, Optional, TextIO, Tuple, Set

JsonObj = Dict[str, Any]

try:
    from .cond_utils import effective_val, compress_loop_iterations
except Exception:
    from cond_utils import effective_val, compress_loop_iterations


def slim_call(c: JsonObj) -> JsonObj:
    return {
        'invocation_id': c.get('invocation_id'),
        'call_file': c.get('call_file'),
        'call_line': c.get('call_line'),
        'call_expr': c.get('call_expr'),
    }


def slim_cond(e: JsonObj) -> JsonObj:
    return {
        'file': e.get('file'),
        'line': e.get('line'),
        'cond_norm': e.get('cond_norm'),
        'cond_hash': e.get('cond_hash'),
        'cond_kind': e.get('cond_kind'),
        'val': e.get('val'),
        'flip': e.get('norm_flip'),
    }


def derive_func_and_matches(conds: List[JsonObj], meta: Optional[JsonObj]) -> Tuple[Optional[str], List[JsonObj]]:
    func_hash = None
    for ev in conds:
        fh = ev.get('func')
        if fh:
            func_hash = fh
            break
    matches: List[JsonObj] = []
    if func_hash and meta and 'static_chains_by_func' in meta:
        _c = compress_loop_iterations(conds)
        rseq = [(e.get('cond_hash'), effective_val(e)) for e in _c]
        static_list = meta['static_chains_by_func'].get(func_hash, [])
        chain_id = 0
        for hseq, source in static_list:
            if hseq == rseq:
                matches.append({'source': source, 'chain_id': chain_id, 'cond_hashes': hseq})
            chain_id += 1
    return func_hash, matches


def emit_triple(out_fp: TextIO, test_info: JsonObj, assert_ev: JsonObj,
                prefix_calls: List[JsonObj], oracle_calls: List[JsonObj],
                inv_cond: Dict[int, List[JsonObj]], dedupe_conds: bool,
                meta: Optional[JsonObj] = None,
                approx_ctx: Optional[Dict[str, Any]] = None) -> None:
    cond_chains: Dict[str, List[JsonObj]] = {}
    inv_info: Dict[str, JsonObj] = {}

    for iid, conds in inv_cond.items():
        # Always compress loops for both display and matching
        conds_comp = compress_loop_iterations(conds)
        if dedupe_conds:
            seen: Set[Any] = set()
            deduped: List[JsonObj] = []
            for ev in conds_comp:
                h = ev.get('cond_hash')
                if h in seen:
                    continue
                seen.add(h)
                deduped.append(slim_cond(ev))
            cond_chains[str(iid)] = deduped
            func_hash, matches = derive_func_and_matches(conds_comp, meta)
        else:
            cond_chains[str(iid)] = [slim_cond(ev) for ev in conds_comp]
            func_hash, matches = derive_func_and_matches(conds_comp, meta)

        inv_entry: JsonObj = {}
        if func_hash:
            inv_entry['func_hash'] = func_hash
            if meta and 'functions_by_hash' in meta:
                finfo = meta['functions_by_hash'].get(func_hash)
                if isinstance(finfo, dict):
                    sig = finfo.get('signature')
                    if sig:
                        inv_entry['signature'] = sig
        if matches:
            inv_entry['matched_static'] = matches
        elif approx_ctx and approx_ctx.get('enabled') and meta:
            try:
                matcher = approx_ctx.get('matcher')
                if matcher is not None:
                    topk = int(approx_ctx.get('topk', 3))
                    thr = float(approx_ctx.get('threshold', 0.6))
                    approx = matcher.match(func_hash, conds_comp, topk=topk, threshold=thr)
                    if approx:
                        inv_entry['approx_static'] = approx
            except Exception:
                pass
        if inv_entry:
            inv_info[str(iid)] = inv_entry

    rec = {
        'test': {
            'suite': test_info.get('suite'),
            'name': test_info.get('name'),
            'full': test_info.get('full'),
            'file': test_info.get('file'),
            'line': test_info.get('line'),
        },
        'assertion': {
            'assert_id': assert_ev.get('assert_id'),
            'macro': assert_ev.get('macro'),
            'file': assert_ev.get('file'),
            'line': assert_ev.get('line'),
            'raw': assert_ev.get('raw'),
        },
        'prefix': [slim_call(c) for c in prefix_calls],
        'oracle_calls': [slim_call(c) for c in oracle_calls],
        'cond_chains': cond_chains,
        'invocations': inv_info,
    }
    out_fp.write(__import__('json').dumps(rec, ensure_ascii=False) + '\n')
