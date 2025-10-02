#!/usr/bin/env python3
import argparse
import json
import sys
import os
from typing import Dict, Any, List, Optional, TextIO

# Prefer package-relative imports; fall back to absolute when executed as a script
try:
    from .runtime_utils import open_maybe_gz, TestState, should_keep_test as _skt_impl
    from .meta_loader import load_meta as _load_meta_impl
    from .emitter import emit_triple as _emit_impl
except Exception:
    from runtime_utils import open_maybe_gz, TestState, should_keep_test as _skt_impl  # type: ignore
    from meta_loader import load_meta as _load_meta_impl  # type: ignore
    from emitter import emit_triple as _emit_impl  # type: ignore


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


def should_keep_test(test_info: Optional[JsonObj], suite_filter: Optional[str], name_filter: Optional[str]) -> bool:
    # Delegated to runtime_utils implementation (kept wrapper for stable API)
    return _skt_impl(test_info, suite_filter, name_filter)


def emit_triple(out_fp: TextIO, test_info: JsonObj, assert_ev: JsonObj,
                prefix_calls: List[JsonObj], oracle_calls: List[JsonObj],
                inv_cond: Dict[int, List[JsonObj]], dedupe_conds: bool,
                meta: Optional[JsonObj] = None,
                approx_ctx: Optional[Dict[str, Any]] = None) -> None:
    # Delegate to emitter module (kept wrapper for stable entrypoint and type hints)
    _emit_impl(out_fp, test_info, assert_ev, prefix_calls, oracle_calls, inv_cond, dedupe_conds, meta, approx_ctx)


def load_meta(meta_dir: str) -> JsonObj:
    # Delegate to meta_loader module
    return _load_meta_impl(meta_dir)


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
