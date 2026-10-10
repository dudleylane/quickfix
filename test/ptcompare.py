#!/usr/bin/env python3
"""Compares pt runs of two builds and fails on a regression (#93).

    ptcompare.py BASE1 HEAD1 BASE2 HEAD2

Each argument is one pt run's output. The two builds are run alternately --
base, head, base, head -- so that drift in the machine's load lands on both
sides. A benchmark regresses when the head's median is slower than the base's
by more than THRESHOLD in both pairs, which keeps one disturbed run from
failing the comparison. Benchmarks faster than MIN_US are reported but never
fail it: at a few nanoseconds per operation the run-to-run variation on this
runner reaches 10%, more than any regression worth gating on.
"""

import re
import sys

THRESHOLD = 0.10
MIN_US = 0.2


def medians(path):
    result, name = {}, None
    with open(path) as f:
        for line in f:
            if line.strip() and not line.startswith((' ', 'TSC')):
                name = line.strip()
            match = re.search(r'median ([0-9.]+) us', line)
            if match and name:
                result[name] = float(match.group(1))
    return result


def main(argv):
    if len(argv) != 5:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    base1, head1, base2, head2 = (medians(p) for p in argv[1:])
    if not base1 or not head1:
        print('FAILED: a pt run printed no benchmarks', file=sys.stderr)
        return 1

    regressions = []
    print(f"{'benchmark':72} {'base us':>9} {'head us':>9} {'pair 1':>7} {'pair 2':>7}")
    for name, b1 in base1.items():
        if name not in head1 or name not in base2 or name not in head2:
            print(f'{name[:72]:72} only in one build')
            continue
        d1 = head1[name] / b1 - 1
        d2 = head2[name] / base2[name] - 1
        base = min(b1, base2[name])
        head = min(head1[name], head2[name])
        flag = ''
        if base >= MIN_US and d1 > THRESHOLD and d2 > THRESHOLD:
            flag = '  REGRESSION'
            regressions.append(name)
        print(f'{name[:72]:72} {base:9.4f} {head:9.4f} {d1:+7.1%} {d2:+7.1%}{flag}')

    if regressions:
        print(f'FAILED: {len(regressions)} benchmark(s) slower by more than {THRESHOLD:.0%} in both pairs')
        return 1
    print(f'No benchmark of {MIN_US} us or more is slower by more than {THRESHOLD:.0%} in both pairs')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
