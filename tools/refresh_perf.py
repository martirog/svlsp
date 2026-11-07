#!/usr/bin/env python3
"""refresh_perf.py -- how a project DB keeps up with a changing tree.

The baseline for plan.md §6.34 (S0): a project DB is built once, then the
tree changes the way it does in real work, and `svlsp --build-db` is run
again on the same DB after each change. Each rerun is timed, and its
result is compared row by row with a fresh build of the same tree, so the
table says both what a refresh costs and whether it is right.

    tools/refresh_perf.py uvm      # the UVM source, git tags 2020.3.0 -> 2020.3.1
    tools/refresh_perf.py gen      # tools/gen_perf_project.py, variant A -> B
    tools/refresh_perf.py gen --json tests/perf/baselines/refresh_gen.json

Steps, on a git working tree of the workload in a temp dir:
    full build      a fresh DB of tree A (on the work dir's disk, timed)
    no change       rerun at once
    edit one file   one included file gets a line at its top (every row
                    of that file moves one line down)
    revert the edit back to tree A
    switch A -> B   `git checkout B` (a "branch switch")
    switch B -> A   and back

The fresh builds the reruns are checked against go to /dev/shm (only their
rows matter). Tables compared: every table with per-file rows, keyed by
file path, without row ids; `files` by path.

The UVM workload is one compilation unit (uvm.sv includes everything), so
any change recompiles all of it; the generated one has ~120 units.
"""

import argparse
import collections
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UVM_REPO = "/home/martin/src/verilator_test/uvm-core"
UVM_DB = "/home/martin/src/verilator_test/uvm-core/src/uvm.db"


def git(repo, *args):
    subprocess.run(["git", "-C", str(repo)] + list(args), check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------- workloads

def setup_uvm(work, args):
    """(repo, config path, file to edit, rev A, rev B)."""
    repo = work / "repo"
    subprocess.run(["git", "clone", "--quiet", "--shared", "--no-checkout",
                    args.uvm_repo, str(repo)], check=True)
    git(repo, "checkout", "--quiet", args.rev_a)
    cfg = repo / "src/.svlsp.json"
    cfg.write_text(json.dumps({"files": ["uvm.sv"], "includeDirs": ["."],
                               "defines": {"UVM_NO_DPI": ""}}, indent=2) + "\n")
    return repo, cfg, repo / "src/base/uvm_object.svh", args.rev_a, args.rev_b


def setup_gen(work, args):
    repo = work / "repo"
    repo.mkdir()
    git(repo, "init", "--quiet")
    for variant in ("A", "B"):
        subprocess.run([sys.executable, str(ROOT / "tools/gen_perf_project.py"), str(repo),
                        "--library-db", args.uvm_db, "--variant", variant],
                       check=True, stdout=subprocess.DEVNULL)
        git(repo, "add", "-A")
        git(repo, "-c", "user.name=refresh_perf", "-c", "user.email=refresh_perf@localhost",
            "commit", "--quiet", "-m", f"variant {variant}")
        git(repo, "tag", f"variant-{variant}")
    git(repo, "checkout", "--quiet", "variant-A")
    return (repo, repo / ".svlsp.json", repo / "agents/agent_5/agent_5_driver.svh",
            "variant-A", "variant-B")


# ---------------------------------------------------------------- builds

def build(server, cfg, out):
    """(seconds, max RSS MB, summary line) of `svlsp --build-db cfg --output out`."""
    t0 = time.perf_counter()
    proc = subprocess.Popen([server, "--build-db", str(cfg), "--output", str(out)],
                            cwd=str(cfg.parent), stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    err = proc.stderr.read().decode(errors="replace")
    _, status, rusage = os.wait4(proc.pid, 0)
    secs = time.perf_counter() - t0
    summary = err.replace("\r", "\n").strip().splitlines()[-1:] or [""]
    if os.WEXITSTATUS(status) != 0:
        sys.exit(f"refresh_perf: build failed: {err[-2000:]}")
    return secs, rusage.ru_maxrss / 1024, summary[0]


def dump(db_path):
    """{table: Counter of rows}, rows keyed by file path, without ids."""
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    paths = dict(con.execute("SELECT id, path FROM files"))
    out = {"files": collections.Counter((p,) for p in paths.values())}
    tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type = 'table'")]
    for t in tables:
        cols = [r[1] for r in con.execute(f"PRAGMA table_info({t})")]
        fcols = [c for c in cols if c.endswith("file_id")]
        if not fcols or t == "files":
            continue
        keep = [c for c in cols if c != "id"]
        rows = collections.Counter()
        for row in con.execute(f"SELECT {', '.join(keep)} FROM {t}"):
            rows[tuple(paths.get(v, f"<file {v}>") if c in fcols else v
                       for c, v in zip(keep, row))] += 1
        out[t] = rows
    con.close()
    return out


def diff(got, want):
    """{table: (missing, extra)} for the tables that differ."""
    res = {}
    for t in sorted(set(got) | set(want)):
        g, w = got.get(t, collections.Counter()), want.get(t, collections.Counter())
        missing, extra = sum((w - g).values()), sum((g - w).values())
        if missing or extra:
            res[t] = (missing, extra)
    return res


# ---------------------------------------------------------------- run

def run(args):
    work = Path(args.workdir or tempfile.mkdtemp(prefix=f"svlsp-refresh-{args.workload}."))
    work.mkdir(parents=True, exist_ok=True)
    shm = Path(tempfile.mkdtemp(prefix="svlsp-refresh-fresh.", dir="/dev/shm"))
    setup = setup_uvm if args.workload == "uvm" else setup_gen
    repo, cfg, edit_file, rev_a, rev_b = setup(work, args)
    db = work / "project.db"
    print(f"refresh_perf: workload {args.workload} in {work}; server {args.server}")

    fresh_cache = {}

    def fresh(tree):
        if tree not in fresh_cache:
            out = shm / f"fresh-{len(fresh_cache)}.db"
            secs, _, _ = build(args.server, cfg, out)
            fresh_cache[tree] = (dump(out), secs)
            out.unlink()
        return fresh_cache[tree]

    rows = []

    def step(name, tree, full=False):
        secs, rss, summary = build(args.server, cfg, db)
        got = dump(db)
        if full:
            fresh_cache[tree] = (got, secs)
        want, fresh_secs = fresh(tree)
        d = diff(got, want)
        n_rows = sum(sum(c.values()) for c in want.values())
        row = {"step": name, "tree": tree, "seconds": secs, "max_rss_mb": rss,
               "fresh_seconds": fresh_secs, "rows": n_rows, "differs": d,
               "summary": summary}
        rows.append(row)
        verdict = "same as a fresh build" if not d else "STALE: " + ", ".join(
            f"{t} -{m}/+{e}" for t, (m, e) in d.items())
        print(f"  {name:16} {secs:8.1f} s  {rss:7.0f} MB  {verdict}")
        sys.stdout.flush()

    step("full build", "A", full=True)
    step("no change", "A")
    original = edit_file.read_text()
    edit_file.write_text("// refresh_perf: an edit\n" + original)
    step("edit one file", "A+edit")
    git(repo, "checkout", "--quiet", "--", str(edit_file.relative_to(repo)))
    step("revert the edit", "A")
    git(repo, "checkout", "--quiet", rev_b)
    step("switch A -> B", "B")
    git(repo, "checkout", "--quiet", rev_a)
    step("switch B -> A", "A")

    changed = subprocess.run(["git", "-C", str(repo), "diff", "--name-only", rev_a, rev_b],
                             stdout=subprocess.PIPE, universal_newlines=True,
                             check=True).stdout.split()
    print(f"\n  A -> B changes {len(changed)} files; the edited file is "
          f"{edit_file.relative_to(repo)}; fresh builds on /dev/shm took "
          + (", ".join(f"{k} {v[1]:.1f} s" for k, v in fresh_cache.items() if k != "A")
             or "-"))
    result = {"workload": args.workload, "server": args.server, "rev_a": rev_a,
              "rev_b": rev_b, "files_changed_a_to_b": len(changed),
              "edited_file": str(edit_file.relative_to(repo)), "steps": rows,
              "host": os.uname().nodename,
              "server_version": subprocess.run([args.server, "--version"],
                                               stdout=subprocess.PIPE,
                                               universal_newlines=True).stdout.strip()}
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n")
    shutil.rmtree(shm, ignore_errors=True)
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("workload", choices=["uvm", "gen"])
    ap.add_argument("--server", default=str(ROOT / "build/release/svlsp"))
    ap.add_argument("--uvm-repo", default=os.environ.get("SVLSP_UVM_REPO", UVM_REPO),
                    help="a git checkout of uvm-core (cloned --shared; left untouched)")
    ap.add_argument("--uvm-db", default=os.environ.get("SVLSP_UVM_DB", UVM_DB),
                    help="the UVM library DB the generated project attaches")
    ap.add_argument("--rev-a", default="2020.3.0")
    ap.add_argument("--rev-b", default="2020.3.1")
    ap.add_argument("--workdir", help="work here instead of a temp dir")
    ap.add_argument("--json", help="also write the results here")
    ap.add_argument("--keep", action="store_true", help="keep the work dir")
    return run(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
