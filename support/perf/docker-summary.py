#!/usr/bin/env python3
"""Summaries for support/perf/docker-compare.sh.

    docker-summary.py times times.txt
        Median per build ("old", "new") of every scenario and measure in
        run-compare.sh output, the change, and each build's range.
    docker-summary.py profile old.ann new.ann
        Instruction counts per function, summed over every process's
        callgrind_annotate output; the total and the 35 largest changes.
"""

import collections
import re
import statistics
import sys


def times(path):
    data = collections.defaultdict(list)
    with open(path) as f:
        for line in f:
            fields = line.split()
            if len(fields) != 5 or fields[3] == "FAILED":
                continue
            build, _, scenario, measure, value = fields
            data[(scenario, measure, build)].append(float(value))
    for scenario, measure in sorted({(s, m) for s, m, _ in data}):
        old = data.get((scenario, measure, "old"))
        new = data.get((scenario, measure, "new"))
        if not old or not new:
            continue
        mo, mn = statistics.median(old), statistics.median(new)
        change = 100 * (mn - mo) / mo if mo else 0.0
        print(f"{scenario:6} {measure:22} old {mo:12.3f} new {mn:12.3f} "
              f"{change:+6.1f}%   old {min(old):.10g}-{max(old):.10g}  "
              f"new {min(new):.10g}-{max(new):.10g}")


LINE = re.compile(r"\s*([\d,]+)\s+\([^)]*\)\s+(.*)$")


def annotations(path):
    counts, total = collections.defaultdict(int), 0
    with open(path) as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            value = int(m.group(1).replace(",", ""))
            name = m.group(2).strip()
            # The two source trees, and glibc's two copies of its string
            # functions, under one name each.
            name = re.sub(r"(^|/src/)(old|new)/", r"\1X/", name)
            name = re.sub(r"\[[^\]]*/(ld-linux[^/\]]*|libc)\.so[^\]]*\]",
                          "[libc]", name)
            if "PROGRAM TOTALS" in name:
                total += value
            else:
                counts[name] += value
    return counts, total


def profile(old_path, new_path):
    old, old_total = annotations(old_path)
    new, new_total = annotations(new_path)
    change = 100 * (new_total - old_total) / old_total if old_total else 0.0
    print(f"instructions: old {old_total:,} new {new_total:,} "
          f"({new_total - old_total:+,}, {change:+.1f}%)")
    names = sorted(set(old) | set(new),
                   key=lambda k: -abs(new.get(k, 0) - old.get(k, 0)))
    for name in names[:35]:
        a, b = old.get(name, 0), new.get(name, 0)
        print(f"{b - a:+14,}  old {a:14,}  new {b:14,}  {name[:120]}")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "times":
        times(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "profile":
        profile(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
