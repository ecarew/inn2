#!/usr/bin/env python3
"""Write the performance comparison report (doc/spec/performance-comparison.md,
section 6) from run-compare.sh output.

    report.py --header header.txt --static static.txt --times times.txt
              [--leaks leaks.txt] [--coverage coverage.txt]
              [--notes notes.md] build ...

The builds are the report's columns, in order; the first is the baseline
that every other column is compared with.

Inputs (all "key value" text, so each pass can be rerun on its own):
  header.txt    "name: value" lines for the report header
  static.txt    "<build> loc <component> N", "<build> bin <program> unstripped
                stripped", "<build> lib <library> bytes"
  times.txt     run-compare.sh: "<build> <rep> <scenario> <measure> <value>"
                or "<build> <rep> <scenario> FAILED <reason>"
  leaks.txt     run-compare.sh with PERF_LEAKS=1, same form
  coverage.txt  "<build> <component> <lines-covered> <lines-total>"
"""

import argparse
import collections
import statistics

LEAK_NOTE = ("The test could lead to damage to the platform's working "
             "environment and represents a likely cybersecurity risk.")

SCENARIOS = [
    ("W1", "Feed intake: 40,000 articles streamed with TAKETHIS"),
    ("W2", "Duplicate offers: the same 40,000 offered with CHECK"),
    ("W3", "Outgoing feed: innfeed streams every article to a peer"),
    ("W4", "Readers: 20 concurrent nnrpd sessions"),
    ("W5", "Daily maintenance: expire and expireover"),
    ("W6", "Rebuild: makehistory over the spool"),
    ("server", "innd over its whole run (W1 to W5)"),
]

# Which program a measure or a leak line belongs to.
PROGRAM_OF = {"W1": "innd", "W2": "innd", "W3": "innfeed", "W4": "nnrpd",
              "W6": "makehistory", "server": "innd"}


def program(scenario, measure):
    for p in ("innfeed", "nnrpd", "expireover", "expire", "makehistory",
              "innd"):
        if measure.startswith(p):
            return p
    return PROGRAM_OF.get(scenario, "")


def read_runs(path):
    values = collections.defaultdict(list)
    failures = collections.defaultdict(list)
    if not path:
        return values, failures
    with open(path) as f:
        for line in f:
            parts = line.split(maxsplit=4)
            if len(parts) < 5:
                continue
            build, _rep, scenario, measure, value = parts
            if measure == "FAILED":
                failures[(build, scenario)].append(value.strip())
                continue
            try:
                values[(build, scenario, measure)].append(float(value))
            except ValueError:
                pass
    return values, failures


def unit(measure):
    if "_per_s" in measure:
        return "", 1
    if measure.endswith("_bytes"):
        return "MB", 1e6
    if measure.endswith("_s"):
        return "s", 1
    return "", 1


def label(measure):
    names = {
        "wall_s": "wall time",
        "articles_per_s": "articles per second",
        "spool_files_left": "article files left",
    }
    if measure in names:
        return names[measure]
    for suffix, text in (("_user_s", "user CPU"), ("_sys_s", "system CPU"),
                         ("_cpu_s", "CPU"),
                         ("_peak_rss_bytes", "peak resident memory"),
                         ("_peak_footprint_bytes", "peak memory footprint"),
                         ("_wall_s", "wall time")):
        if measure.endswith(suffix):
            return f"{measure[:-len(suffix)]} {text}"
    return measure


def leaked_by(key, b, sc, prog):
    """True if leak record key is program prog (or one of its processes,
    such as nnrpd-session for nnrpd) in build b and scenario sc."""
    return (bool(prog) and key[0] == b and key[1] == sc
            and (key[2] == prog or key[2].startswith(prog + "-")))


def plural(n, word):
    return f"{n} {word}" if n == 1 else f"{n} {word}s"


def cell(vals, scale):
    med = statistics.median(vals)
    if len(vals) == 1:
        return f"{med / scale:.2f}", med
    return (f"{med / scale:.2f} [{min(vals) / scale:.2f}–"
            f"{max(vals) / scale:.2f}]"), med


def pct(new, base):
    if not base:
        return ""
    return f"{(new / base - 1) * 100:+.1f}%"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True)
    ap.add_argument("--static", required=True)
    ap.add_argument("--times", required=True)
    ap.add_argument("--leaks")
    ap.add_argument("--coverage")
    ap.add_argument("--notes", help="markdown appended after Findings")
    ap.add_argument("builds", nargs="+")
    a = ap.parse_args()
    builds = a.builds
    base = builds[0]
    out = []
    w = out.append

    w("# INN++ Performance Comparison\n")
    with open(a.header) as f:
        for line in f:
            if line.strip():
                k, _, v = line.partition(":")
                w(f"- **{k.strip()}:** {v.strip()}")
    w("")
    w("Cells are the median of the runs, with the range in brackets.  Each "
      "Δ compares that column with the first (the baseline).  **FAILED**: "
      "the program could not finish the scenario correctly (reason under "
      "Findings).  Spec: `doc/spec/performance-comparison.md`.\n")

    def header_row(first):
        cols = [first]
        for b in builds:
            cols.append(b)
            if b != base:
                cols.append("Δ")
        w("| " + " | ".join(cols) + " |")
        w("|" + "---|" * len(cols))

    # --- Static measures
    static = collections.defaultdict(dict)
    with open(a.static) as f:
        for line in f:
            p = line.split()
            if len(p) >= 4:
                static[(p[1], p[2])][p[0]] = [float(x) for x in p[3:]]
    for kind, title, idx, scale, fmt in (
            ("loc", "Lines of code (code lines, no comments or blank lines)",
             0, 1, "{:,.0f}"),
            ("bin", "Binary size, unstripped (KB)", 0, 1e3, "{:,.0f}"),
            ("bin", "Binary size, stripped (KB)", 1, 1e3, "{:,.0f}"),
            ("lib", "Library size (KB)", 0, 1e3, "{:,.0f}")):
        rows = sorted(k for k in static if k[0] == kind)
        if not rows:
            continue
        w(f"## {title}\n")
        header_row("")
        for key in rows:
            cols = [key[1]]
            bv = static[key].get(base)
            for b in builds:
                v = static[key].get(b)
                cols.append(fmt.format(v[idx] / scale) if v else "")
                if b != base:
                    cols.append(pct(v[idx], bv[idx]) if v and bv else "")
            w("| " + " | ".join(cols) + " |")
        w("")

    # --- Run measures
    values, failures = read_runs(a.times)
    leaks, leak_failures = read_runs(a.leaks)
    findings = []
    footnoted = set()
    leaked = {}
    for (b, sc, m), v in leaks.items():
        if m.endswith("_leaks") and max(v) > 0:
            prog = m[:-len("_leaks")]
            nbytes = max(leaks.get((b, sc, prog + "_leaked_bytes"), [0]))
            leaked[(b, sc, prog)] = (int(max(v)), int(nbytes))

    w("## Workload\n")
    for sc, title in SCENARIOS:
        measures = sorted({m for (b, s, m) in values if s == sc})
        failed_here = [b for b in builds if (b, sc) in failures]
        if not measures and not failed_here:
            continue
        w(f"### {sc}: {title}\n")
        header_row("measure")
        for m in measures:
            u, scale = unit(m)
            cols = [label(m) + (f" ({u})" if u else "")]
            basemed = None
            for b in builds:
                if (b, sc) in failures:
                    cols.append("**FAILED**")
                    if b != base:
                        cols.append("")
                    continue
                vals = values.get((b, sc, m))
                if not vals:
                    cols.append("")
                    if b != base:
                        cols.append("")
                    continue
                text, med = cell(vals, scale)
                prog = program(sc, m)
                if any(leaked_by(k, b, sc, prog) for k in leaked):
                    text += " [^leak]"
                    footnoted.add((b, sc, prog))
                cols.append(text)
                if b == base:
                    basemed = med
                else:
                    cols.append(pct(med, basemed) if basemed else "")
            w("| " + " | ".join(cols) + " |")
        w("")
        for b in failed_here:
            for reason in sorted(set(failures[(b, sc)])):
                findings.append(f"**{b}, {sc}: FAILED.** {reason}")

    # --- Leaks
    if a.leaks:
        w("## Leaks (macOS leaks, malloc stack logging)\n")
        progs = sorted({(s, m[:-len("_leaks")]) for (b, s, m) in leaks
                        if m.endswith("_leaks")})
        header_row("scenario: program")
        for sc, prog in progs:
            cols = [f"{sc}: {prog}"]
            for b in builds:
                v = leaks.get((b, sc, prog + "_leaks"))
                by = leaks.get((b, sc, prog + "_leaked_bytes"))
                if v is None:
                    cols.append("")
                else:
                    t = (f"{plural(int(max(v)), 'leak')}, "
                         f"{plural(int(max(by or [0])), 'byte')}")
                    if max(v) > 0:
                        t += " [^leak]"
                    cols.append(t)
                if b != base:
                    cols.append("")
            w("| " + " | ".join(cols) + " |")
        w("")
        for (b, sc, prog), (n, nbytes) in sorted(leaked.items()):
            new = (base, sc, prog) not in leaked
            findings.append(
                f"**{b}, {sc}: {prog} leaks** {plural(n, 'block')}, "
                f"{plural(nbytes, 'byte')}"
                + (" (not in the baseline)." if new and b != base else "."))
        for (b, sc), reasons in leak_failures.items():
            for r in sorted(set(reasons)):
                findings.append(f"**{b}, {sc} (leak pass):** {r}")

    # --- Coverage
    if a.coverage:
        cov = collections.defaultdict(dict)
        with open(a.coverage) as f:
            for line in f:
                p = line.split()
                if len(p) == 4:
                    cov[p[1]][p[0]] = (int(p[2]), int(p[3]))
        w("## Line coverage under the workload\n")
        header_row("component")
        for comp in sorted(cov):
            cols = [comp]
            bc = cov[comp].get(base)
            for b in builds:
                c = cov[comp].get(b)
                if c and c[1]:
                    cols.append(f"{c[0] * 100 / c[1]:.1f}% ({c[0]:,}/{c[1]:,})")
                else:
                    cols.append("")
                if b != base:
                    if c and bc and c[1] and bc[1]:
                        d = c[0] * 100 / c[1] - bc[0] * 100 / bc[1]
                        cols.append(f"{d:+.1f} pts")
                        if abs(d) > 5:
                            findings.append(
                                f"**{b}: {comp} coverage** {d:+.1f} points "
                                "against the baseline.")
                    else:
                        cols.append("")
            w("| " + " | ".join(cols) + " |")
        w("")

    # --- Regressions over 5%
    for sc, _ in SCENARIOS:
        for m in sorted({m for (b, s, m) in values if s == sc}):
            bv = values.get((base, sc, m))
            if not bv or "per_s" in m or m == "spool_files_left":
                continue
            bmed = statistics.median(bv)
            for b in builds[1:]:
                v = values.get((b, sc, m))
                if v and bmed and statistics.median(v) / bmed > 1.05:
                    findings.append(
                        f"**{b}, {sc}: {label(m)}** "
                        f"{pct(statistics.median(v), bmed)} against the "
                        "baseline.")

    w("## Findings\n")
    if findings:
        for f_ in findings:
            w(f"- {f_}")
    else:
        w("- None: no failures, no leaks, no regression over 5%.")
    w("")
    if a.notes:
        with open(a.notes) as f:
            w(f.read().rstrip() + "\n")
    if footnoted or leaked:
        w(f"[^leak]: {LEAK_NOTE}")
        w("")
    print("\n".join(out))


if __name__ == "__main__":
    main()
