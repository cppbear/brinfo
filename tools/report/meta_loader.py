#!/usr/bin/env python3
from typing import Dict, Any, List, Optional, Tuple
import json
import os
import sys

JsonObj = Dict[str, Any]


def load_meta(meta_dir: str) -> JsonObj:
    functions_by_hash: Dict[str, JsonObj] = {}
    conditions_by_hash: Dict[str, JsonObj] = {}
    conditions_by_id: Dict[int, JsonObj] = {}
    static_chains_by_func: Dict[str, List[Tuple[List[str], str]]] = {}

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
            analysis_versions['conditions'] = data.get('analysis_version')
            items = data.get('conditions') or []
            if isinstance(items, list):
                for item in items:
                    if not isinstance(item, dict):
                        continue
                    cid = item.get('id')
                    if isinstance(cid, int):
                        conditions_by_id[cid] = item
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
            conds_raw = ch.get('sequence') or []
            hseq: List[str] = []
            for c in conds_raw:
                if not isinstance(c, dict):
                    continue
                cid = c.get('cond_id')
                cval = c.get('value')
                if isinstance(cid, int):
                    info = conditions_by_id.get(cid)
                    if info:
                        h = info.get('hash')
                        if h:
                            hseq.append((str(h), cval))
            add_chain(str(fh) if fh else None, hseq, path)
    except Exception:
        pass

    path = os.path.join(meta_dir, "functions.meta.json")
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        if isinstance(data, dict):
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

    av_funcs = analysis_versions.get('functions')
    av_conds = analysis_versions.get('conditions')
    av_chains = analysis_versions.get('chains')
    if av_funcs and av_conds and av_chains:
        if not (av_funcs == av_conds == av_chains):
            sys.stderr.write(
                f"[brinfo_report] warning: meta analysis_version mismatch: "
                f"functions={av_funcs}, conditions={av_conds}, chains={av_chains}\n"
            )

    return {
        'functions_by_hash': functions_by_hash,
        'conditions_by_hash': conditions_by_hash,
        'static_chains_by_func': static_chains_by_func,
    }
