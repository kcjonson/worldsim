#!/usr/bin/env python3
"""Resolve every #include under libs/ and print a per-library include matrix.

Each #include is resolved against each library's declared include root plus
the including file's directory (quoted includes only), the way the build
would find it. Roots are the library directories under libs/, plus libs/
itself for planet-view, which exports it. The first hit decides which
library the include lands in; an include that resolves nowhere is counted
as external. Production files and test/bench files are tallied separately.

Output, in order: the matrix for all files, the matrix for production files
only (*.test.cpp and *.bench.cpp excluded), ambiguous resolutions, includes
that break the documented layering, and the engine -> ui coupling surface.
Cells are counts of #include lines; self-includes sit on the diagonal.

Usage: python -I tools/resolve_includes.py [--root PATH]
"""
import argparse
import collections
import os
import re
import sys

LIBS = ["foundation", "geometry", "renderer", "ui", "world", "planet-view", "game-systems", "engine"]
LIBS_ORDER = LIBS + ["engine(assets)", "external"]
SOURCE_EXTS = (".h", ".hpp", ".cpp", ".cc", ".inl")
INC = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

ALLOWED = {
    "foundation": {"foundation", "external"},
    "geometry": {"geometry", "foundation", "external"},
    "renderer": {"renderer", "foundation", "external"},
    "ui": {"ui", "renderer", "foundation", "external"},
    "world": {"world", "renderer", "foundation", "external"},
    "planet-view": {"planet-view", "world", "renderer", "foundation", "external"},
    "game-systems": {"game-systems", "world", "renderer", "foundation", "external"},
    "engine": set(LIBS_ORDER),
    "engine(assets)": set(LIBS_ORDER),
}


def default_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def include_roots(root):
    libs = os.path.join(root, "libs")
    return [
        ("foundation", os.path.join(libs, "foundation")),
        ("geometry", os.path.join(libs, "geometry")),
        ("renderer", os.path.join(libs, "renderer")),
        ("ui", os.path.join(libs, "ui")),
        ("world", os.path.join(libs, "world")),
        ("planet-view", os.path.join(libs, "planet-view")),
        ("planet-view", libs),  # planet-view exports libs/ so "planet-view/X.h" works
        ("game-systems", os.path.join(libs, "game-systems")),
        ("engine", os.path.join(libs, "engine")),  # also covers "assets/X.h" -> libs/engine/assets/X.h
    ]


def lib_of(root, path):
    rel = os.path.relpath(path, os.path.join(root, "libs")).replace("\\", "/")
    top = rel.split("/")[0]
    if top == "engine" and rel.startswith("engine/assets/"):
        return "engine(assets)"
    return top


def resolve(roots, src_file, form, target):
    cands = []
    if form == '"':
        p = os.path.normpath(os.path.join(os.path.dirname(src_file), target))
        if os.path.isfile(p):
            cands.append(p)
    for _name, inc_root in roots:
        p = os.path.normpath(os.path.join(inc_root, target))
        if os.path.isfile(p) and p not in cands:
            cands.append(p)
    return cands


def collect_rows(root):
    """Return (srclib, srcfile_rel, line, form, target, dstlib, resolved_rel, ambiguous) tuples."""
    roots = include_roots(root)
    rows = []
    for lib in LIBS:
        base = os.path.join(root, "libs", lib)
        for dp, _dn, fn in os.walk(base):
            for f in fn:
                if not f.endswith(SOURCE_EXTS):
                    continue
                full = os.path.join(dp, f)
                rel = os.path.relpath(full, root).replace("\\", "/")
                srclib = lib_of(root, full)
                try:
                    with open(full, encoding="utf-8", errors="replace") as fh:
                        lines = fh.read().splitlines()
                except Exception as e:
                    print("ERR", full, e, file=sys.stderr)
                    continue
                for i, line in enumerate(lines, 1):
                    m = INC.match(line)
                    if not m:
                        continue
                    form, target = m.group(1), m.group(2)
                    cands = resolve(roots, full, form, target)
                    if cands:
                        dst = lib_of(root, cands[0])
                        rres = os.path.relpath(cands[0], root).replace("\\", "/")
                        amb = len({lib_of(root, c) for c in cands}) > 1
                    else:
                        dst = "external"
                        rres = ""
                        amb = False
                    rows.append((srclib, rel, i, form, target, dst, rres, amb))
    return rows


def print_matrix(m, title):
    print("\n=== %s ===" % title)
    cols = LIBS_ORDER
    print("src\\dst".ljust(16) + "".join(c[:12].rjust(13) for c in cols))
    for s in LIBS + ["engine(assets)"]:
        if s not in m:
            continue
        print(s.ljust(16) + "".join(str(m[s].get(c, 0)).rjust(13) for c in cols))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=default_root(),
                    help="repo root containing libs/ (default: the repo this script lives in)")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    if not os.path.isdir(os.path.join(root, "libs")):
        ap.error("no libs/ directory under %s" % root)

    rows = collect_rows(root)

    # ---- matrix
    mat = collections.defaultdict(collections.Counter)
    mat_prod = collections.defaultdict(collections.Counter)
    for srclib, rel, _i, _form, _target, dst, _rres, _amb in rows:
        mat[srclib][dst] += 1
        if ".test." not in rel and ".bench." not in rel:
            mat_prod[srclib][dst] += 1
    print_matrix(mat, "INCLUDE MATRIX, all files (counts of #include lines; self-includes on the diagonal)")
    print_matrix(mat_prod, "INCLUDE MATRIX, production only (excluding *.test.cpp / *.bench.cpp)")

    # ---- ambiguities
    ambs = [r for r in rows if r[7]]
    print("\n=== AMBIGUOUS resolutions (header name exists in >1 lib) ===")
    for r in ambs:
        print("  %s:%d  %s%s  -> picked %s (%s)" % (r[1], r[2], r[3], r[4], r[5], r[6]))
    if not ambs:
        print("  none")

    # ---- violations
    print("\n=== VIOLATIONS (includes that cross the documented layering) ===")
    for s in ["foundation", "geometry", "renderer", "ui", "world", "planet-view", "game-systems"]:
        v = [r for r in rows if r[0] == s and r[5] not in ALLOWED[s]]
        print("\n-- %s: %d violating include(s)" % (s, len(v)))
        for r in v:
            print("  %s:%d  #include %s%s%s  -> %s [%s]" % (r[1], r[2], r[3], r[4], '>' if r[3] == '<' else '"', r[6], r[5]))

    # ---- engine -> ui coupling surface
    print("\n=== ENGINE -> UI includes (allowed; coupling surface) ===")
    eu = [r for r in rows if r[0] in ("engine", "engine(assets)") and r[5] == "ui"]
    by_hdr = collections.defaultdict(list)
    by_dir = collections.Counter()
    for r in eu:
        by_hdr[r[6]].append("%s:%d" % (r[1], r[2]))
        by_dir["/".join(r[1].split("/")[:3])] += 1
    print("total engine->ui include lines: %d across %d engine files" % (len(eu), len({r[1] for r in eu})))
    print("\nby engine subdir:")
    for d, c in sorted(by_dir.items(), key=lambda x: -x[1]):
        print("  %-45s %d" % (d, c))
    print("\nby ui header (which ui headers engine pulls in):")
    for h, sites in sorted(by_hdr.items(), key=lambda x: -len(x[1])):
        print("  %-50s %d" % (h, len(sites)))
    print("\nengine files that include ui (file:line -> header):")
    for r in sorted(eu, key=lambda r: (r[1], r[2])):
        print("  %s:%d -> %s" % (r[1], r[2], r[6]))

    # ---- generic engine slice: what do application/scene/input/clipboard include?
    print("\n=== ENGINE generic slice (application/, scene/, input/, clipboard/): non-self includes ===")
    for r in sorted(rows, key=lambda r: (r[1], r[2])):
        if r[0] == "engine" and r[1].split("/")[2] in ("application", "scene", "input", "clipboard") and r[5] not in ("external",):
            print("  %s:%d -> %s [%s]" % (r[1], r[2], r[6], r[5]))


if __name__ == "__main__":
    main()
