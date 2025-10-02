#!/usr/bin/env python3
import argparse
import gzip
import io
import json
import sys
import os
from typing import Dict, Any, List, Optional, TextIO, Set, Tuple


def open_maybe_gz(path: str) -> TextIO:
    """Open a text file that may be gzip-compressed (by .gz suffix)."""
    if path.endswith('.gz'):
        return io.TextIOWrapper(gzip.open(path, 'rb'), encoding='utf-8')
    return open(path, 'r', encoding='utf-8')


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description='BrInfo offline report: extract <prefix, oracle, cond_chain> per assertion')
    ap.add_argument('--logs', required=True, help='NDJSON runtime log (optionally .gz)')
    ap.add_argument('--meta', default=None, help='Optional meta directory for static-chain matching and enrichment')
    ap.add_argument('--out', required=True, help='Output JSONL path')
    ap.add_argument('--suite', default=None, help='Filter suite regex (substring match)')
    ap.add_argument('--test', dest='test_name', default=None, help='Filter test name regex (substring match)')
    ap.add_argument('--dedupe-conds', action='store_true', help='Deduplicate condition chain by cond_hash')
    # Approximate matching options
    ap.add_argument('--approx-match', action='store_true', default=False,
                    help='When exact static-chain matching is empty, compute approximate matches')
    ap.add_argument('--approx-topk', type=int, default=3,
                    help='Top-K approximate static matches to output (default: 3)')
    ap.add_argument('--approx-threshold', type=float, default=0.6,
                    help='Minimum score threshold [0..1] for approximate matches (default: 0.6)')
    return ap.parse_args()


JsonObj = Dict[str, Any]


def _effective_val(ev: JsonObj) -> bool:
    """Return the effective boolean for a condition event, considering normalization flip.

    Effective value is (val XOR norm_flip) so it aligns with the static truth recorded in meta.
    """
    return bool(ev.get('val')) ^ bool(ev.get('norm_flip'))


def compress_loop_iterations(conds: List[JsonObj]) -> List[JsonObj]:
    """Compress repeated loop iterations in a linear sequence of condition events.

    Rationale:
    - Runtime logs will record loop heads (`cond_kind == 'LOOP'`) once per iteration (typically: True at entry/continue,
      and a final False to exit), interleaved with body conditions which can repeat across iterations.
    - Static meta chains only distinguish whether a loop is entered (and the body path once), not how many iterations.

    Strategy:
        - For each loop head (cond_hash with cond_kind == 'LOOP') when we see a True (RAW val is True), we:
            1) Keep that True.
            2) Keep only the first iteration body (events until the next event for the same loop head).
            3) Skip subsequent iterations entirely and DROP the final False (exit). This matches static chains which
                 generally do not include an explicit exit record once a loop is entered.
    - Nested loops are handled recursively by compressing the body slice.
    - Loop head False without a preceding True (zero-iteration) is preserved.

    Notes:
    - This is a best-effort normalization that greatly improves alignment with static chains. Some corner cases like
      do-while with early breaks are still represented sensibly (first observed body kept; no duplicate bodies).
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

        # Loop head encountered
        loop_hash = ev.get('cond_hash')
        # Use RAW runtime value to decide whether the loop is entered
        ev_true = bool(ev.get('val'))
        if not ev_true:
            # Zero-iteration check (or exit point) — keep single False and move on
            out.append(ev)
            i += 1
            continue

        # Keep the first True for this loop head
        out.append(ev)

        # Find the next event for the same loop head to delimit the first body slice
        j = i + 1
        while j < n:
            ej = conds[j]
            if is_loop(ej) and ej.get('cond_hash') == loop_hash:
                break
            j += 1

        # Compress and append the first-iteration body (if any)
        if j > i + 1:
            out.extend(compress_loop_iterations(conds[i + 1:j]))

        # Find the last False (exit) for this loop head after j
        k: Optional[int] = None
        p = j
        while p < n:
            ep = conds[p]
            # Use RAW False to detect exit
            if is_loop(ep) and ep.get('cond_hash') == loop_hash and not bool(ep.get('val')):
                k = p  # update to latest exit we see
            p += 1

        # If an explicit exit False exists, DROP it (we already entered the loop) but advance past it; otherwise continue at j
        if k is not None:
            i = k + 1
        else:
            i = j

    return out


class TestState:
    def __init__(self):
        # Buffer of invocations (in_oracle=0) since last cut (test_start or last assertion)
        self.buffer_prefix: List[JsonObj] = []
        # For the currently open assertion, its prefix snapshot captured at assertion event
        self.curr_prefix: List[JsonObj] = []
        # Current assertion event
        self.open_assert: Optional[JsonObj] = None
        # Invocations with in_oracle=1 in current assertion window
        self.oracle_calls: List[JsonObj] = []
        # Accumulate all cond events by invocation across the test
        self.inv_cond_all: Dict[int, List[JsonObj]] = {}
        # From test_start
        self.test_info: Optional[JsonObj] = None



def should_keep_test(test_info: Optional[JsonObj], suite_filter: Optional[str], name_filter: Optional[str]) -> bool:
    if not test_info:
        return True
    if suite_filter and suite_filter not in test_info.get('suite', ''):
        return False
    if name_filter and name_filter not in test_info.get('name', '') and name_filter not in test_info.get('full', ''):
        return False
    return True


def emit_triple(out_fp: TextIO, test_info: JsonObj, assert_ev: JsonObj,
                prefix_calls: List[JsonObj], oracle_calls: List[JsonObj],
                inv_cond: Dict[int, List[JsonObj]], dedupe_conds: bool,
                meta: Optional[JsonObj] = None,
                approx_ctx: Optional[Dict[str, Any]] = None) -> None:
    """Emit a single triple record to out_fp as JSON line.

    Notes:
    - cond_chains events include 'flip' which mirrors runtime 'norm_flip'.
        Matching uses pairs (cond_hash, val ^ flip) to align with static truth values.
    - When --dedupe-conds is set, cond_chains are de-duplicated for display only;
        matching still runs on the full original sequence (conds argument).
    """

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

    # print(f"[brinfo_report] emit_triple: test={test_info.get('full')}, assert_id={assert_ev.get('assert_id')}, "
    #       f"prefix_calls={len(prefix_calls)}, oracle_calls={len(oracle_calls)}, inv_cond={len(inv_cond)}")

    cond_chains: Dict[str, List[JsonObj]] = {}
    inv_info: Dict[str, JsonObj] = {}

    # Helper to derive function hash and match static chains if meta available
    def derive_func_and_matches(conds: List[JsonObj]) -> Tuple[Optional[str], List[JsonObj]]:
        # print(f"[brinfo_report] derive_func_and_matches: conds={conds}")
        func_hash = None
        for ev in conds:
            fh = ev.get('func')
            if fh:
                func_hash = fh
                break
        matches: List[JsonObj] = []
        if func_hash and meta and 'static_chains_by_func' in meta:
            # Match against compressed sequence to remove repeated loop iterations
            _c = compress_loop_iterations(conds)
            rseq = [(e.get('cond_hash'), _effective_val(e)) for e in _c]
            # print(f"[brinfo_report] func_hash={func_hash}, rseq={rseq}")
            # print(f"static_chains_by_func: {meta['static_chains_by_func']}")
            static_list: List[Tuple[List[str], str]] = meta['static_chains_by_func'].get(func_hash, [])
            # print(f"[brinfo_report] static_list={static_list}")
            chain_id = 0
            for hseq, source in static_list:
                if hseq == rseq:
                    matches.append({'source': source, 'chain_id': chain_id, 'cond_hashes': hseq})
                chain_id += 1
        # print(f"[brinfo_report] func_hash={func_hash}, conds={len(conds)}, matches={len(matches)}")
        return func_hash, matches

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
            func_hash, matches = derive_func_and_matches(conds_comp)
        else:
            cond_chains[str(iid)] = [slim_cond(ev) for ev in conds_comp]
            func_hash, matches = derive_func_and_matches(conds_comp)

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
            # Attempt approximate matching using compressed sequence
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
    out_fp.write(json.dumps(rec, ensure_ascii=False) + '\n')


def load_meta(meta_dir: str) -> JsonObj:
    """Load meta information from a directory.

    Files expected in meta_dir:
    - conditions.meta.json: { analysis_version, conditions: [ { id, hash, cond_norm, kind, ... } ] }
        Used to build id -> condition and hash -> condition indices.
    - chains.meta.json: { analysis_version, chains: [ { func_hash, sequence: [ { cond_id, value }, ... ] } ] }
        Builds static_chains_by_func with sequences of (cond_hash, value) pairs per function.
    - functions.meta.json: { analysis_version, functions: [ { hash, name?, signature, ... } ] }
        Builds functions_by_hash for enrichment.

    Returns: dict with consolidated indices:
    - functions_by_hash
    - conditions_by_hash
    - static_chains_by_func: func_hash -> [ ( [(cond_hash, value), ...], source_path ), ... ]
    Also warns when analysis_version across the three files is inconsistent.
    """
    functions_by_hash: Dict[str, JsonObj] = {}
    conditions_by_hash: Dict[str, JsonObj] = {}
    # Internal: map condition id -> condition entry (to resolve chains.sequence)
    conditions_by_id: Dict[int, JsonObj] = {}
    # Note: each static sequence entry is a list of (cond_hash, value) pairs
    # associated to a func_hash, along with the source file path it came from.
    static_chains_by_func: Dict[str, List[Tuple[List[str], str]]] = {}

    # Track analysis_version across meta files to ensure they are consistent
    analysis_versions: Dict[str, Optional[str]] = {
        'functions': None,
        'conditions': None,
        'chains': None,
    }

    def add_chain(func_hash: Optional[str], cond_hashes: List[str], source: str) -> None:
        if not func_hash:
            return
        static_chains_by_func.setdefault(func_hash, []).append((cond_hashes, source))

    path = os.path.join(meta_dir, "conditions.meta.json")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        if isinstance(data, dict):
            # Expected shape: { analysis_version, conditions: [ { id, hash, cond_norm, kind, ... } ] }
            analysis_versions['conditions'] = data.get('analysis_version')
            items = data.get('conditions') or []
            if isinstance(items, list):
                for item in items:
                    if not isinstance(item, dict):
                        continue
                    # Map by id (int) for chain resolution
                    cid = item.get('id')
                    if isinstance(cid, int):
                        conditions_by_id[cid] = item
                    # Optional: also map by cond hash string for external lookups
                    ch = item.get('hash') or item.get('cond_hash')
                    if ch:
                        conditions_by_hash[str(ch)] = item
    except Exception:
        pass

    path = os.path.join(meta_dir, "chains.meta.json")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        seqs: List[JsonObj] = []
        if isinstance(data, dict):
            # Expected shape: { analysis_version, chains: [ { func_hash, sequence: [ {cond_id, value} ] } ] }
            analysis_versions['chains'] = data.get('analysis_version')
            seqs = data.get('chains') or []
        elif isinstance(data, list):
            seqs = data
        else:
            seqs = []
        for ch in seqs:
            if not isinstance(ch, dict):
                continue
            fh = ch.get('func_hash') or ch.get('func')
            # Use 'sequence' with cond_id/value pairs per the meta schema
            conds_raw = ch.get('sequence') or []
            hseq: List[str] = []
            for c in conds_raw:
                if not isinstance(c, dict):
                    continue
                # Important: cond_id may be 0; do not use truthiness to test presence
                cid = c.get('cond_id')
                cval = c.get('value')
                if isinstance(cid, int):
                    info = conditions_by_id.get(cid)
                    if info:
                        h = info.get('hash')
                        if h:
                            hseq.append((str(h), cval))
            # print(f"[brinfo_report] chains.meta.json: func_hash={fh}, conds={conds_raw}, hseq={hseq}")
            add_chain(str(fh) if fh else None, hseq, path)
    except Exception:
        pass

    path = os.path.join(meta_dir, "functions.meta.json")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        if isinstance(data, dict):
            # Expected shape: { analysis_version, functions: [ { hash, name, signature, ... } ] }
            analysis_versions['functions'] = data.get('analysis_version')
            items = data.get('functions') or []
            if isinstance(items, list):
                for item in items:
                    if isinstance(item, dict):
                        fh = item.get('hash')
                        if fh:
                            functions_by_hash[str(fh)] = item
    except Exception:
        pass

    # Verify analysis_version consistency if all present
    av_funcs = analysis_versions.get('functions')
    av_conds = analysis_versions.get('conditions')
    av_chains = analysis_versions.get('chains')
    if av_funcs and av_conds and av_chains:
        if not (av_funcs == av_conds == av_chains):
            sys.stderr.write(
                f"[brinfo_report] warning: meta analysis_version mismatch: "
                f"functions={av_funcs}, conditions={av_conds}, chains={av_chains}\n"
            )

    # print(f"[brinfo_report] loaded meta: "
    #       f"functions={len(functions_by_hash)}, conditions={len(conditions_by_hash)}, "
    #       f"chains={sum(len(v) for v in static_chains_by_func.values())}\n"
    #       )

    return {
        'functions_by_hash': functions_by_hash,
        'conditions_by_hash': conditions_by_hash,
        'static_chains_by_func': static_chains_by_func,
    }


def main() -> None:
    args = parse_args()

    # Open output upfront so we can emit while parsing
    if args.out == '-':
        out_fp = sys.stdout
    else:
        out_fp = open(args.out, 'w', encoding='utf-8')

    meta: Optional[JsonObj] = None
    approx_ctx: Optional[Dict[str, Any]] = None
    if args.meta:
        try:
            meta = load_meta(args.meta)
        except Exception:
            meta = None
    # Initialize approximate matcher if requested and meta is available
    if args.approx_match and meta:
        try:
            # Local import to avoid mandatory dependency when not used
            from . import approx_match as _approx_mod  # type: ignore
        except Exception:
            try:
                import approx_match as _approx_mod  # type: ignore
            except Exception:
                _approx_mod = None  # type: ignore
        if _approx_mod is not None:
            try:
                matcher = _approx_mod.ApproxMatcher.from_meta(meta)
                approx_ctx = {
                    'enabled': True,
                    'topk': args.approx_topk,
                    'threshold': args.approx_threshold,
                    'matcher': matcher,
                }
            except Exception:
                approx_ctx = None

    tests: Dict[int, TestState] = {}

    with open_maybe_gz(args.logs) as fp:
        for line in fp:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except Exception:
                continue

            ev_type = ev.get('type')
            test_id = ev.get('test_id')
            if test_id is None:
                continue

            st = tests.setdefault(test_id, TestState())

            if ev_type == 'test_start':
                st.test_info = {
                    'suite': ev.get('suite'),
                    'name': ev.get('name'),
                    'full': ev.get('full'),
                    'file': ev.get('file'),
                    'line': ev.get('line'),
                }
                st.buffer_prefix.clear()
                st.open_assert = None
                st.curr_prefix = []
                st.oracle_calls.clear()
                st.inv_cond_all.clear()
                continue

            if ev_type == 'invocation_start':
                inv = {
                    'invocation_id': ev.get('invocation_id'),
                    'in_oracle': ev.get('in_oracle', 0),
                    'call_file': ev.get('call_file'),
                    'call_line': ev.get('call_line'),
                    'call_expr': ev.get('call_expr'),
                }
                if st.open_assert:
                    if inv['in_oracle']:
                        st.oracle_calls.append(inv)
                    else:
                        # This belongs to the next assertion's prefix
                        st.buffer_prefix.append(inv)
                else:
                    # Before first assertion: part of first prefix
                    st.buffer_prefix.append(inv)
                continue

            if ev_type == 'cond':
                iid = ev.get('invocation_id')
                if iid is not None:
                    st.inv_cond_all.setdefault(iid, []).append(ev)
                continue

            if ev_type == 'assertion':
                # Emit the previous assertion (closing window)
                if st.open_assert and st.test_info and should_keep_test(st.test_info, args.suite, args.test_name):
                    # Filter conds for calls in this assertion window
                    iids = {c['invocation_id'] for c in st.curr_prefix} | {c['invocation_id'] for c in st.oracle_calls}
                    inv_cond = {iid: st.inv_cond_all.get(iid, []) for iid in iids}
                    emit_triple(out_fp, st.test_info, st.open_assert,
                                st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta, approx_ctx)
                # Start new assertion window: snapshot current buffer as prefix
                st.open_assert = ev
                st.curr_prefix = st.buffer_prefix
                st.buffer_prefix = []
                st.oracle_calls = []
                continue

            if ev_type == 'test_end':
                # Close any open assertion window on test end
                if st.open_assert and st.test_info and should_keep_test(st.test_info, args.suite, args.test_name):
                    iids = {c['invocation_id'] for c in st.curr_prefix} | {c['invocation_id'] for c in st.oracle_calls}
                    inv_cond = {iid: st.inv_cond_all.get(iid, []) for iid in iids}
                    emit_triple(out_fp, st.test_info, st.open_assert,
                                st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta, approx_ctx)
                st.open_assert = None
                st.curr_prefix = []
                st.buffer_prefix.clear()
                st.oracle_calls.clear()
                st.inv_cond_all.clear()
                continue

    # Re-iterate states to flush any assertions not yet emitted (in case logs ended without test_end)
    for st in tests.values():
        if st.open_assert and st.test_info and should_keep_test(st.test_info, args.suite, args.test_name):
            iids = {c['invocation_id'] for c in st.curr_prefix} | {c['invocation_id'] for c in st.oracle_calls}
            inv_cond = {iid: st.inv_cond_all.get(iid, []) for iid in iids}
            emit_triple(out_fp, st.test_info, st.open_assert, st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta, approx_ctx)

    if args.out != '-':
        out_fp.close()


if __name__ == '__main__':
    main()
