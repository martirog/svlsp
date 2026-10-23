#!/usr/bin/env python3
"""lsp_perf.py -- latency benchmark for svlsp alone, over stdio LSP.

Runs a scenario (tests/perf/<name>.json) against a fresh server: prepares
a temporary workspace, opens one file, then times every request in the
scenario `--repeat` times, an edit -> diagnostics round trip, and a typing
session (one character at a time, completion after each). Each request's
client round trip is paired with the server's own time from its
`--log-requests` line (`run`, and `wait` for the data lock). Results are
judged against tests/perf/budgets.json on their p90.

    tools/lsp_perf.py tests/perf/uvm.json                 # release build
    tools/lsp_perf.py tests/perf/small.json --repeat 20 --check
    tools/lsp_perf.py tests/perf/uvm.json --json out.json
    tools/lsp_perf.py tests/perf/uvm.json --baseline tests/perf/baselines/uvm.json

The round trip is measured to the last byte of the response, before JSON
decoding, so it is the server plus the pipe -- an editor adds its own
decoding and rendering on top (tests/perf/emacs_perf.sh measures that).

It also reads the server's memory per phase (startup, first compile,
library load, requests, edits, typing, idle): the RSS at each phase's end
and the peak during it; `--max-rss MB` fails a run whose peak goes over a
per-user limit. `--baseline FILE` compares the run with an earlier `--json`
result and exits 1 on a regression (a code review's check, `make
perf-check`).

`--prepare-only DIR` just writes the scenario's workspace into DIR (used
by the Emacs harness, so both run the same workspace).
"""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------- scenario

def expand_vars(value, env):
    if isinstance(value, str):
        return re.sub(r"\$\{(\w+)\}", lambda m: env[m.group(1)], value)
    if isinstance(value, list):
        return [expand_vars(v, env) for v in value]
    if isinstance(value, dict):
        return {k: expand_vars(v, env) for k, v in value.items()}
    return value


def scenario_vars(scenario):
    """The scenario's ${VAR}s: from the environment, else their default."""
    env = {}
    for name, spec in scenario.get("vars", {}).items():
        env[name] = os.environ.get(spec.get("env", ""), spec.get("default", ""))
        if spec.get("mustExist") and not Path(env[name]).exists():
            sys.exit(f"lsp_perf: {name} = {env[name]} does not exist "
                     f"(set {spec.get('env')})")
    return env


def prepare_workspace(scenario, workdir):
    env = scenario_vars(scenario)
    if "copyDir" in scenario:
        # Relative to the repo, or absolute (a ${VAR}); `exclude` globs are
        # skipped (a corpus's own library DBs, say).
        src = ROOT / expand_vars(scenario["copyDir"], env)
        ignore = shutil.ignore_patterns(*scenario.get("exclude", []))
        skip = ignore(str(src), [e.name for e in src.iterdir()])
        for entry in src.iterdir():  # into an existing dir (3.6: no dirs_exist_ok)
            if entry.name in skip:
                continue
            if entry.is_dir():
                shutil.copytree(str(entry), str(Path(workdir) / entry.name), ignore=ignore)
            else:
                shutil.copy(str(entry), str(workdir))
    for dest, src in scenario.get("files", {}).items():
        shutil.copy(ROOT / src, Path(workdir) / dest)
    for dest, content in scenario.get("write", {}).items():
        Path(workdir, dest).write_text(json.dumps(expand_vars(content, env), indent=2) + "\n")


def position_of(text, at):
    """LSP position for an `at` spec: the first occurrence of `find`, then
    of `sub` inside it; plus `offset` characters (default 1, inside the
    word) or "end" (just after it). ASCII text, so UTF-16 == chars."""
    idx = text.find(at["find"])
    if idx < 0:
        sys.exit(f"lsp_perf: text not found: {at['find']!r}")
    length = len(at["find"])
    if "sub" in at:
        idx += at["find"].index(at["sub"])
        length = len(at["sub"])
    off = at.get("offset", 1)
    idx += length if off == "end" else off
    line = text.count("\n", 0, idx)
    return {"line": line, "character": idx - (text.rfind("\n", 0, idx) + 1)}


def offset_of(text, at):
    pos = position_of(text, at)
    lines = text.split("\n")
    return sum(len(l) + 1 for l in lines[:pos["line"]]) + pos["character"]


# ---------------------------------------------------------------- client

class Client:
    def __init__(self, cmd, stderr_path, timeout=900):
        self.timeout = timeout  # seconds for a response or a compile's diagnostics
        self.stderr = open(stderr_path, "wb")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.stderr)
        self.next_id = 1
        self.responses = {}
        self.diagnostics = []  # (time, params)
        self.cv = threading.Condition()
        self.write_lock = threading.Lock()
        self.eof = False
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        out = self.proc.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    with self.cv:
                        self.eof = True
                        self.cv.notify_all()
                    return
                line = line.strip()
                if not line:
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":")[1])
            body = out.read(length)
            t = time.perf_counter()
            msg = json.loads(body)
            if "method" in msg and "id" in msg:  # a server request: accept it
                self._send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
                continue
            with self.cv:
                if "id" in msg:
                    self.responses[msg["id"]] = (t, msg, len(body))
                elif msg.get("method") == "textDocument/publishDiagnostics":
                    self.diagnostics.append((t, msg["params"]))
                self.cv.notify_all()

    def _send(self, obj):
        body = json.dumps(obj).encode()
        with self.write_lock:
            self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
            self.proc.stdin.flush()

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=None):
        """(round-trip ms, response message, response bytes)."""
        rid = self.next_id
        self.next_id += 1
        t0 = time.perf_counter()
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        with self.cv:
            timeout = timeout or self.timeout
            if not self.cv.wait_for(lambda: rid in self.responses or self.eof, timeout):
                sys.exit(f"lsp_perf: {method} timed out after {timeout} s")
            if self.eof and rid not in self.responses:
                sys.exit(f"lsp_perf: server exited during {method}")
            t1, msg, size = self.responses.pop(rid)
        if "error" in msg:
            sys.exit(f"lsp_perf: {method} failed: {msg['error']}")
        return (t1 - t0) * 1000, msg, size

    def wait_diagnostics(self, uri, version, since, timeout=None):
        """Time of the first diagnostics for (uri, version) after `since`."""
        def found():
            for t, p in self.diagnostics:
                if t >= since and p["uri"] == uri and p.get("version") == version:
                    return t
            return None
        with self.cv:
            timeout = timeout or self.timeout
            if not self.cv.wait_for(lambda: found() is not None or self.eof, timeout):
                sys.exit(f"lsp_perf: no diagnostics for version {version} within {timeout} s")
            return found()


class Memory:
    """The server's memory per phase, with its parse workers (Linux /proc).

    `phase(name)` closes the phase that started at the previous call. Each
    row has the RSS at its end of the server plus its child processes (the
    parse workers, plan.md 6.33 C1) -- `rss_kb` -- split into the server's
    own (`server_rss_kb`, its anonymous part `anon_kb`: the rest is mapped
    files such as an attached library DB) and the workers' (`worker_rss_kb`),
    and the peak of that total during the phase (`peak_kb`). The peak is the
    larger of a 10 ms sample of the total and the kernel's high-water marks
    (VmHWM, reset at each phase start through /proc/<pid>/clear_refs) of
    the server plus each worker alive at the end; the sample is what sees a
    worker that was recycled during the phase. Where the reset is not
    allowed, the sample alone (it can miss a short spike)."""

    def __init__(self, pid):
        self.pid = pid
        self.rows = []
        self.sampled = 0
        self.lock = threading.Lock()
        self.exact = self._reset_peak(pid)
        for c in self.children():
            self._reset_peak(c)
        threading.Thread(target=self._sample, daemon=True).start()

    @staticmethod
    def status(pid):
        """{field: kB} for the Vm*/Rss* lines, or {} once the process is gone."""
        out = {}
        try:
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    key, _, rest = line.partition(":")
                    if key.startswith(("Vm", "Rss")):
                        out[key] = int(rest.split()[0])
        except (OSError, ValueError):
            pass
        return out

    def children(self):
        """The server's child processes' pids."""
        pids = []
        try:
            for task in os.listdir(f"/proc/{self.pid}/task"):
                with open(f"/proc/{self.pid}/task/{task}/children") as f:
                    pids += [int(p) for p in f.read().split()]
        except (OSError, ValueError):
            pass
        return pids

    @staticmethod
    def _reset_peak(pid):
        try:
            with open(f"/proc/{pid}/clear_refs", "w") as f:
                f.write("5")
            return True
        except OSError:
            return False

    def _total(self):
        """(server status, {child pid: status}); server status {} once gone."""
        return self.status(self.pid), {c: self.status(c) for c in self.children()}

    def _sample(self):
        while True:
            server, kids = self._total()
            if not server:
                return
            total = server["VmRSS"] + sum(k.get("VmRSS", 0) for k in kids.values())
            with self.lock:
                self.sampled = max(self.sampled, total)
            time.sleep(0.01)

    def phase(self, name):
        server, kids = self._total()
        if not server:
            return
        worker_rss = sum(k.get("VmRSS", 0) for k in kids.values())
        with self.lock:
            peak = max(self.sampled, server["VmRSS"] + worker_rss)
            if self.exact:
                peak = max(peak, server.get("VmHWM", 0) +
                           sum(k.get("VmHWM", 0) for k in kids.values()))
            self.sampled = 0
        self.rows.append({"name": name, "rss_kb": server["VmRSS"] + worker_rss,
                          "server_rss_kb": server["VmRSS"], "anon_kb": server.get("RssAnon"),
                          "worker_rss_kb": worker_rss, "workers": len(kids),
                          "peak_kb": peak})
        if self.exact:
            self._reset_peak(self.pid)
            for c in kids:
                self._reset_peak(c)

    def summary(self):
        return {"exact_peak": self.exact, "phases": self.rows,
                "peak_kb": max((r["peak_kb"] for r in self.rows), default=0)}


class RequestLog:
    """Reads the server's --log-requests lines as they are appended."""
    LINE = re.compile(r"^\[request\] (\S+).* wait ([0-9.]+) ms, run ([0-9.]+) ms$")

    def __init__(self, path):
        self.path = path
        self.pos = 0

    def new_lines(self):
        try:
            with open(self.path) as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
        except FileNotFoundError:
            return []
        return [m.groups() for m in map(self.LINE.match, data.splitlines()) if m]

    def last(self, method):
        """(wait, run) of the latest new line for `method`, else (None, None)."""
        hit = (None, None)
        for what, wait, run in self.new_lines():
            if what == method:
                hit = (float(wait), float(run))
        return hit


def result_count(method, result):
    if result is None:
        return 0
    if method == "textDocument/completion":
        return len(result["items"] if isinstance(result, dict) else result)
    if method == "textDocument/rename":
        return sum(len(e) for e in result.get("changes", {}).values()) or \
               len(result.get("documentChanges", []))
    if method == "textDocument/signatureHelp":
        return len(result.get("signatures", []))
    if method in ("textDocument/hover", "completionItem/resolve"):
        return 1
    return len(result) if isinstance(result, list) else 1


# ---------------------------------------------------------------- stats

def pct(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, int(round(p / 100 * (len(s) - 1))))]


def summarize(name, tier, client_ms, server_run, server_wait, count, size):
    srun = [v for v in server_run if v is not None]
    swait = [v for v in server_wait if v is not None]
    return {
        "name": name, "tier": tier, "n": len(client_ms),
        "min": min(client_ms), "median": statistics.median(client_ms),
        "p90": pct(client_ms, 90), "max": max(client_ms),
        "server_run_median": statistics.median(srun) if srun else None,
        "server_wait_max": max(swait) if swait else None,
        "results": count, "bytes": size,
    }


def verdict(row, budgets):
    p90 = row["p90"]
    budget = budgets["tiers"][row["tier"]]["budget_ms"]
    if p90 <= budgets["instant_ms"]:
        return "instant"
    if p90 <= budget:
        return "ok"
    if p90 <= budgets["flow_ms"]:
        return "OVER BUDGET"
    return "BREAKS FLOW"


def fmt_ms(v):
    return "-" if v is None else f"{v:8.1f}"


def print_table(rows, budgets):
    print(f"\n{'request':58} {'tier':11} {'n':>3} {'median':>8} {'p90':>8} {'max':>8}"
          f" {'srv run':>8} {'srv wait':>8} {'results':>7} {'bytes':>9}  verdict")
    for r in rows:
        empty = "  (EMPTY RESULT)" if r["results"] == 0 else ""
        print(f"{r['name'][:58]:58} {r['tier']:11} {r['n']:3} {fmt_ms(r['median'])}"
              f" {fmt_ms(r['p90'])} {fmt_ms(r['max'])} {fmt_ms(r['server_run_median'])}"
              f" {fmt_ms(r['server_wait_max'])} {r['results']:7} {r['bytes'] or '-':>9}"
              f"  {r['verdict']}{empty}")
    print("\nbudgets (p90): " + ", ".join(
        f"{t} {v['budget_ms']} ms" for t, v in budgets["tiers"].items())
        + f"; instant <= {budgets['instant_ms']} ms, flow <= {budgets['flow_ms']} ms")


def fmt_mb(kb):
    return "-" if kb is None else f"{kb / 1024:8.1f}"


def print_memory(memory, max_rss_mb=None):
    """The per-phase memory table; True if the peak is over `max_rss_mb`."""
    if not memory or not memory.get("phases"):
        return False
    how = "VmHWM + 10 ms samples" if memory.get("exact_peak") else "sampled every 10 ms"
    print(f"\n{'memory (MB): server + parse workers':40} {'rss at end':>10} {'server':>8}"
          f" {'of it anon':>10} {'workers':>8} {'peak during':>11}   (peak: {how})")
    for p in memory["phases"]:
        # Rows from before the parse worker have the server alone.
        server = p.get("server_rss_kb", p["rss_kb"])
        print(f"{p['name'][:40]:40} {fmt_mb(p['rss_kb']):>10} {fmt_mb(server):>8}"
              f" {fmt_mb(p.get('anon_kb')):>10} {fmt_mb(p.get('worker_rss_kb', 0)):>8}"
              f" {fmt_mb(p['peak_kb']):>11}")
    peak_mb = memory["peak_kb"] / 1024
    over = max_rss_mb is not None and peak_mb > max_rss_mb
    limit = "" if max_rss_mb is None else \
        f" -- limit {max_rss_mb} MB: {'OVER' if over else 'ok'}"
    print(f"peak RSS {peak_mb:.1f} MB (server + workers){limit}")
    return over


# ---------------------------------------------------------------- run

def run(args):
    scenario = json.loads(Path(args.scenario).read_text())
    budgets = json.loads((ROOT / "tests/perf/budgets.json").read_text())
    work = Path(tempfile.mkdtemp(prefix=f"svlsp-perf-{scenario['name']}."))
    prepare_workspace(scenario, work)
    reqlog_path = work / "requests.log"
    reqlog = RequestLog(reqlog_path)

    path = work / scenario["open"]
    uri = path.as_uri()
    text = path.read_text()
    client = Client([args.server, "--log-requests", str(reqlog_path)] + args.server_arg,
                    work / "stderr.log",
                    args.compile_timeout or scenario.get("compileTimeout", 900))
    memory = Memory(client.proc.pid)
    rows = []
    phases = {}

    ms, _, _ = client.request("initialize", {
        "processId": os.getpid(), "rootUri": work.as_uri(),
        "workspaceFolders": [{"uri": work.as_uri(), "name": work.name}],
        "capabilities": {"textDocument": {
            "completion": {"completionItem": {"snippetSupport": False}},
            "hover": {"contentFormat": ["markdown", "plaintext"]}}},
    })
    phases["initialize"] = ms
    memory.phase("initialize")
    client.notify("initialized", {})
    version = 1
    t0 = time.perf_counter()
    client.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "systemverilog", "version": version, "text": text}})
    t = client.wait_diagnostics(uri, version, t0)
    phases["open -> first diagnostics"] = (t - t0) * 1000
    memory.phase("open -> first diagnostics")

    if "ready" in scenario:  # the library loads on the first request for the file
        pos = position_of(text, scenario["ready"]["at"])
        deadline = time.time() + args.timeout
        while True:
            _, msg, _ = client.request("textDocument/completion", {
                "textDocument": {"uri": uri}, "position": pos})
            res = msg.get("result") or []
            items = res["items"] if isinstance(res, dict) else res
            if any(i["label"] == scenario["ready"]["label"] for i in items):
                break
            if time.time() > deadline:
                sys.exit("lsp_perf: library never became visible")
            time.sleep(0.5)
        phases["open -> library visible"] = (time.perf_counter() - t0) * 1000
        memory.phase("library load")

    print(f"svlsp perf: scenario {scenario['name']} -- {scenario['description']}")
    print(f"  server {args.server}; workspace {work}; {args.repeat} runs after {args.warmup} warm-up")
    for k, v in phases.items():
        print(f"  {k:28} {v:9.1f} ms")
    reqlog.new_lines()

    for req in scenario["requests"]:
        if args.only and not re.search(args.only, req["name"]):
            continue
        method = req["method"]
        params = dict(req.get("params", {}))
        if method.startswith("textDocument/"):
            params["textDocument"] = {"uri": uri}
        if "at" in req:
            params["position"] = position_of(text, req["at"])
        if "resolveFrom" in req:
            src = req["resolveFrom"]
            _, msg, _ = client.request("textDocument/completion", {
                "textDocument": {"uri": uri}, "position": position_of(text, src["at"])})
            res = msg["result"]
            items = res["items"] if isinstance(res, dict) else res
            params = next((i for i in items if i["label"] == src["label"]), None)
            if params is None:
                sys.exit(f"lsp_perf: {src['label']} not offered for {req['name']}")
        client_ms, srun, swait = [], [], []
        for i in range(args.warmup + args.repeat):
            reqlog.new_lines()
            ms, msg, size = client.request(method, params)
            wait, run_ms = reqlog.last(method)
            if i >= args.warmup:
                client_ms.append(ms)
                srun.append(run_ms)
                swait.append(wait)
        row = summarize(req["name"], req["tier"], client_ms, srun, swait,
                        result_count(method, msg.get("result")), size)
        row["verdict"] = verdict(row, budgets)
        rows.append(row)
    memory.phase("requests")

    # Edit -> diagnostics: insert, then remove, the scenario's edit text,
    # waiting each time for the diagnostics of the new version (the
    # server debounces didChange by 300 ms; that is part of what the user
    # waits for).
    if "edit" in scenario and not args.only:
        at = offset_of(text, scenario["edit"]["at"])
        edited = text[:at] + scenario["edit"]["text"] + text[at:]
        diag_ms = []
        for i in range(args.warmup + args.repeat):
            version += 1
            body = edited if i % 2 == 0 else text
            t0 = time.perf_counter()
            client.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": body}]})
            t = client.wait_diagnostics(uri, version, t0)
            if i >= args.warmup:
                diag_ms.append((t - t0) * 1000)
        if (args.warmup + args.repeat) % 2:
            version += 1
            t0 = time.perf_counter()
            client.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": text}]})
            client.wait_diagnostics(uri, version, t0)
        srun = [run for what, _, run in reqlog.new_lines() if what == "compile"]
        row = summarize("edit -> diagnostics (incl. 300 ms debounce)", "diagnostics",
                        diag_ms, [float(s) for s in srun], [], 1, 0)
        row["verdict"] = verdict(row, budgets)
        rows.append(row)
        memory.phase("edits (recompiles)")

    # Typing: one character every --type-interval ms, completion after
    # each (as an auto-popup completion UI does); the debounced compile
    # starts 300 ms after the last change and may hold the data lock.
    if "typing" in scenario and not args.only:
        at = offset_of(text, scenario["typing"]["at"])
        typed = scenario["typing"]["text"]
        client_ms, srun, swait = [], [], []
        for n in range(1, len(typed) + 1):
            cur = text[:at] + typed[:n] + text[at:]
            version += 1
            client.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": cur}]})
            if not typed[n - 1].isspace():
                idx = at + n  # the cursor, just after the typed text
                pos = {"line": cur.count("\n", 0, idx),
                       "character": idx - (cur.rfind("\n", 0, idx) + 1)}
                reqlog.new_lines()
                ms, msg, _ = client.request("textDocument/completion", {
                    "textDocument": {"uri": uri}, "position": pos})
                wait, run_ms = reqlog.last("textDocument/completion")
                client_ms.append(ms)
                srun.append(run_ms)
                swait.append(wait)
            time.sleep(args.type_interval / 1000)
        row = summarize(f"typing: completion per keystroke ({args.type_interval} ms apart)",
                        "keystroke", client_ms, srun, swait,
                        result_count("textDocument/completion", msg.get("result")), 0)
        row["verdict"] = verdict(row, budgets)
        rows.append(row)
        version += 1
        t0 = time.perf_counter()
        client.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": text}]})
        client.wait_diagnostics(uri, version, t0)
        memory.phase("typing (completion + recompiles)")

    idle = args.idle if args.idle is not None else scenario.get("idle", 2.0)
    time.sleep(idle)
    memory.phase(f"idle {idle:g} s after")

    client.request("shutdown", None)
    client.notify("exit", None)
    try:
        client.proc.wait(10)
    except subprocess.TimeoutExpired:
        client.proc.kill()

    print_table(rows, budgets)
    mem = memory.summary()
    mem_over = print_memory(mem, args.max_rss)
    result = {"scenario": scenario["name"], "server": args.server,
              "host": host_info(args.server), "settings": {"repeat": args.repeat},
              "phases": phases, "rows": rows, "memory": mem}
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n")
    regressed = compare(result, args.baseline) if args.baseline else False
    if not args.keep:
        shutil.rmtree(work)
    over = [r for r in rows if r["verdict"] in ("OVER BUDGET", "BREAKS FLOW")]
    return 1 if (args.check and over) or mem_over or regressed else 0


def host_info(server):
    """What a baseline was measured on: timings only compare on one machine."""
    cpu = ""
    try:
        cpu = next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo")
                    if l.startswith("model name")), "")
    except OSError:
        pass
    try:
        version = subprocess.run([server, "--version"], stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, universal_newlines=True,
                                 timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        version = ""
    return {"host": os.uname().nodename, "cpu": cpu, "cpus": os.cpu_count(),
            "server_version": version}


# A regression must clear both the relative and the absolute margin: a
# 1 ms request that takes 2 ms is noise, not a 100% regression.
REGRESSION = {"ms_pct": 25.0, "ms_abs": 5.0, "mem_pct": 10.0, "mem_abs_mb": 10.0}


def worse(new, old, pct_limit, abs_limit):
    return (new is not None and old is not None and new > old * (1 + pct_limit / 100)
            and new - old > abs_limit)


def compare(result, baseline_path):
    """Print `result` against a baseline --json file; True on a regression.

    Judged per request on the client p90 and the server's own median run
    (the steadier of the two), and on the peak and end RSS."""
    base = json.loads(Path(baseline_path).read_text())
    if base.get("scenario") != result["scenario"]:
        sys.exit(f"lsp_perf: baseline is for scenario {base.get('scenario')}, "
                 f"not {result['scenario']}")
    bh, rh = base.get("host", {}), result.get("host", {})
    print(f"\nagainst baseline {baseline_path}")
    print(f"  baseline: {bh.get('server_version', '?')} on {bh.get('host', '?')}")
    print(f"  this run: {rh.get('server_version', '?')} on {rh.get('host', '?')}")
    if (bh.get("host"), bh.get("cpu")) != (rh.get("host"), rh.get("cpu")):
        print("  NOTE: another machine -- timings are not comparable, memory is")
    r = REGRESSION
    print(f"  regression: > +{r['ms_pct']:g}% and > +{r['ms_abs']:g} ms (p90 or server run); "
          f"memory > +{r['mem_pct']:g}% and > +{r['mem_abs_mb']:g} MB")
    print(f"\n{'request':58} {'p90 base':>9} {'p90 now':>9} {'srv base':>9} {'srv now':>9}  verdict")
    regressed = False
    old_rows = {row["name"]: row for row in base.get("rows", [])}
    for row in result["rows"]:
        old = old_rows.pop(row["name"], None)
        if old is None:
            print(f"{row['name'][:58]:58} {'-':>9} {fmt_ms(row['p90']):>9} {'-':>9}"
                  f" {fmt_ms(row['server_run_median']):>9}  new")
            continue
        bad = [what for what, key in (("p90", "p90"), ("server", "server_run_median"))
               if worse(row[key], old[key], r["ms_pct"], r["ms_abs"])]
        better = (not bad and old["p90"] - row["p90"] > r["ms_abs"]
                  and row["p90"] < old["p90"] / (1 + r["ms_pct"] / 100))
        regressed |= bool(bad)
        verdict_ = ("REGRESSED (" + ", ".join(bad) + ")") if bad else "faster" if better else "ok"
        if old["results"] != row["results"]:
            verdict_ += f"  results {old['results']} -> {row['results']}"
        print(f"{row['name'][:58]:58} {fmt_ms(old['p90']):>9} {fmt_ms(row['p90']):>9}"
              f" {fmt_ms(old['server_run_median']):>9} {fmt_ms(row['server_run_median']):>9}"
              f"  {verdict_}")
    for name in old_rows:
        print(f"{name[:58]:58} (in the baseline only)")
    om, nm = base.get("memory"), result.get("memory")
    if om and nm:
        old_end = om["phases"][-1]["rss_kb"] if om["phases"] else None
        new_end = nm["phases"][-1]["rss_kb"] if nm["phases"] else None
        for what, o, n in (("peak RSS", om["peak_kb"], nm["peak_kb"]),
                           ("RSS at the end", old_end, new_end)):
            bad = worse(n and n / 1024, o and o / 1024, r["mem_pct"], r["mem_abs_mb"])
            regressed |= bad
            print(f"{what:58} {fmt_mb(o):>9} {fmt_mb(n):>9} MB{'':10}"
                  f"  {'REGRESSED' if bad else 'ok'}")
    print("\nresult: " + ("REGRESSED" if regressed else "no regression"))
    return regressed


def report(path):
    """Print the table for a results file of raw samples (the Emacs harness)."""
    data = json.loads(Path(path).read_text())
    budgets = json.loads((ROOT / "tests/perf/budgets.json").read_text())
    print(f"\n{data.get('client', 'lsp')} perf: scenario {data['scenario']}")
    for k, v in data.get("settings", {}).items():
        print(f"  {k:28} {v}")
    for p in data.get("phases", []):
        print(f"  {p['name']:28} {p['ms']:9.1f} ms")
    rows = []
    for r in data["rows"]:
        if not r["client"]:
            continue
        row = summarize(r["name"], r["tier"], r["client"], r["server_run"],
                        r["server_wait"], r["results"], r.get("bytes"))
        row["verdict"] = verdict(row, budgets)
        rows.append(row)
    print_table(rows, budgets)
    print_memory(data.get("memory"))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scenario", nargs="?", help="tests/perf/<name>.json")
    ap.add_argument("--report", metavar="JSON",
                    help="print the table for a raw-sample results file and exit")
    ap.add_argument("--server", default=str(ROOT / "build/release/svlsp"))
    ap.add_argument("--repeat", type=int, default=10)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--type-interval", type=int, default=150,
                    help="ms between typed characters (150 ~ 80 wpm)")
    ap.add_argument("--only", help="regex: run only matching requests (skips edit/typing)")
    ap.add_argument("--timeout", type=int, default=300, help="seconds to wait for the library")
    ap.add_argument("--compile-timeout", type=int,
                    help="seconds to wait for a response or a compile's diagnostics "
                         "(default: the scenario's compileTimeout, else 900)")
    ap.add_argument("--server-arg", action="append", default=[], metavar="ARG",
                    help="pass ARG to the server (repeatable), e.g. --in-process-parse")
    ap.add_argument("--json", help="also write the results here")
    ap.add_argument("--check", action="store_true", help="exit 1 if any p90 is over budget")
    ap.add_argument("--max-rss", type=float, metavar="MB",
                    help="exit 1 if the server's peak RSS goes over MB (a per-user limit)")
    ap.add_argument("--baseline", metavar="JSON",
                    help="compare with an earlier --json result; exit 1 on a regression")
    ap.add_argument("--idle", type=float,
                    help="seconds to wait after the run before the last memory reading "
                         "(default: the scenario's idle, else 2)")
    ap.add_argument("--keep", action="store_true", help="keep the temp workspace (and its logs)")
    ap.add_argument("--prepare-only", metavar="DIR",
                    help="write the scenario's workspace into DIR and exit")
    args = ap.parse_args()
    if args.report:
        return report(args.report)
    if not args.scenario:
        ap.error("a scenario is required")
    if args.prepare_only:
        scenario = json.loads(Path(args.scenario).read_text())
        prepare_workspace(scenario, args.prepare_only)
        return 0
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
