#!/usr/bin/env python3
"""Fails unless ut's own totals say every test passed, so that a skipped test fails.

    noskips.py REPORT...

ut exits 0 when a test skips, and so does every CI step that runs it, so a
skip would read as a pass: SocketThreadAffinityTests' pinsConnectionThreads
skips with fewer than two CPUs, ThreadedTransportTests'
readWorksOnADescriptorAtOrAboveFdSetsize when RLIMIT_NOFILE cannot rise above
FD_SETSIZE. Each CI run of ut therefore writes a second copy of its console
report to a file (-r console -r console::out=REPORT), and this reads the totals
at its end. Catch2 3.4 prints "All tests passed (...)" only when no test failed
or was skipped; a skip turns the totals into "test cases: N | P passed |
S skipped", after a "SKIPPED:" block for each, which this repeats.

Catch2 cannot fail on a skip itself: -w knows NoAssertions and
UnmatchedTestSpec only. Its JUnit and XML reporters are no way to read one
either: they make Catch2 swap the buffers behind std::cout and std::cerr for
every test case (shouldRedirectStdOut), and engine threads write to std::cerr.

A report with no totals line fails too, so that a check of nothing cannot pass.
"""

import re
import sys

TOTALS = re.compile(r"^(All tests passed|test cases:|No tests ran)")
SKIPPED = re.compile(r"^(.*): SKIPPED:$")


def check(path):
    """Prints what the report at path shows and returns whether it passes."""
    try:
        with open(path, encoding="utf-8", errors="replace") as report:
            lines = report.read().splitlines()
    except OSError as error:
        print(f"::error::{path}: no ut report to read ({error})")
        return False
    totals = [line for line in lines if TOTALS.match(line)]
    if not totals:
        print(f"::error::{path} has no totals line, so it cannot show that no test was skipped")
        return False
    if totals[-1].startswith("All tests passed"):
        print(f"{path}: {totals[-1]}")
        return True
    for number, line in enumerate(lines):
        skipped = SKIPPED.match(line)
        if skipped:
            # The reason follows: "explicitly with message:", then the message.
            reason = [text.strip() for text in lines[number + 1 : number + 3] if text.strip()]
            print(f"::error::{path}: a test skipped at {skipped.group(1)}: {' '.join(reason)}")
    print(f'::error::{path}: ut\'s totals are not "All tests passed": {totals[-1]}')
    return False


def main(paths):
    if not paths:
        print("usage: noskips.py REPORT...", file=sys.stderr)
        return 2
    results = [check(path) for path in paths]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
