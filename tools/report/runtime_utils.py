#!/usr/bin/env python3
from typing import Dict, Any, List, Optional, TextIO
import gzip
import io

JsonObj = Dict[str, Any]


def open_maybe_gz(path: str) -> TextIO:
    """Open a text file that may be gzip-compressed (by .gz suffix)."""
    if path.endswith('.gz'):
        return io.TextIOWrapper(gzip.open(path, 'rb'), encoding='utf-8')
    return open(path, 'r', encoding='utf-8')


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
