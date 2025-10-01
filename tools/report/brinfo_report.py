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
    ap.add_argument('--meta', default=None, help='Optional meta directory (unused for core logic)')
    ap.add_argument('--out', required=True, help='Output JSONL path')
    ap.add_argument('--suite', default=None, help='Filter suite regex (substring match)')
    ap.add_argument('--test', dest='test_name', default=None, help='Filter test name regex (substring match)')
    ap.add_argument('--dedupe-conds', action='store_true', help='Deduplicate condition chain by cond_hash')
    return ap.parse_args()


JsonObj = Dict[str, Any]


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
                meta: Optional[JsonObj] = None) -> None:
    """Emit a single triple record to out_fp as JSON line."""

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

    print(f"[brinfo_report] emit_triple: test={test_info.get('full')}, assert_id={assert_ev.get('assert_id')}, "
          f"prefix_calls={len(prefix_calls)}, oracle_calls={len(oracle_calls)}, inv_cond={len(inv_cond)}")

    cond_chains: Dict[str, List[JsonObj]] = {}
    inv_info: Dict[str, JsonObj] = {}

    # Helper to derive function hash and match static chains if meta available
    def derive_func_and_matches(conds: List[JsonObj]) -> Tuple[Optional[str], List[JsonObj]]:
        print(f"[brinfo_report] derive_func_and_matches: conds={conds}")
        func_hash = None
        for ev in conds:
            fh = ev.get('func')
            if fh:
                func_hash = fh
                break
        matches: List[JsonObj] = []
        if func_hash and meta and 'static_chains_by_func' in meta:
            rseq = [(e.get('cond_hash'), bool(e.get('val')) ^ bool(e.get('norm_flip'))) for e in conds]
            print(f"[brinfo_report] func_hash={func_hash}, rseq={rseq}")
            # print(f"static_chains_by_func: {meta['static_chains_by_func']}")
            static_list: List[Tuple[List[str], str]] = meta['static_chains_by_func'].get(func_hash, [])
            print(f"[brinfo_report] static_list={static_list}")
            chain_id = 0
            for hseq, source in static_list:
                if hseq == rseq:
                    matches.append({'source': source, 'chain_id': chain_id, 'cond_hashes': hseq})
                chain_id += 1
        print(f"[brinfo_report] func_hash={func_hash}, conds={len(conds)}, matches={len(matches)}")
        return func_hash, matches

    for iid, conds in inv_cond.items():
        if dedupe_conds:
            seen: Set[Any] = set()
            deduped: List[JsonObj] = []
            for ev in conds:
                h = ev.get('cond_hash')
                if h in seen:
                    continue
                seen.add(h)
                deduped.append(slim_cond(ev))
            cond_chains[str(iid)] = deduped
            func_hash, matches = derive_func_and_matches(conds)
        else:
            cond_chains[str(iid)] = [slim_cond(ev) for ev in conds]
            func_hash, matches = derive_func_and_matches(conds)

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
    """Load meta information from a directory. Supports:
    - functions.meta.json: function hash -> info
    - conditions.meta.json: cond hash -> info
    - chains.meta.json: list of chains with func and cond hashes
    Returns a dict with consolidated indices.
    """
    functions_by_hash: Dict[str, JsonObj] = {}
    conditions_by_hash: Dict[str, JsonObj] = {}
    # Internal: map condition id -> condition entry (to resolve chains.sequence)
    conditions_by_id: Dict[int, JsonObj] = {}
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
                cid = c.get('cond_id')
                cval = c.get('value')
                print(cid)
                if isinstance(cid, int):
                    info = conditions_by_id.get(cid)
                    print(info)
                    if info:
                        h = info.get('hash')
                        if h:
                            hseq.append((str(h), cval))
            print(f"[brinfo_report] chains.meta.json: func_hash={fh}, conds={conds_raw}, hseq={hseq}")
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

    print(f"[brinfo_report] loaded meta: "
          f"functions={len(functions_by_hash)}, conditions={len(conditions_by_hash)}, "
          f"chains={sum(len(v) for v in static_chains_by_func.values())}\n"
          )

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
    if args.meta:
        try:
            meta = load_meta(args.meta)
        except Exception:
            meta = None

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
                                st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta)
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
                                st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta)
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
            emit_triple(out_fp, st.test_info, st.open_assert, st.curr_prefix, st.oracle_calls, inv_cond, args.dedupe_conds, meta)

    if args.out != '-':
        out_fp.close()


if __name__ == '__main__':
    main()
