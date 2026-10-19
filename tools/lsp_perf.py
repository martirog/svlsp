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

The round trip is measured to the last byte of the response, before JSON
decoding, so it is the server plus the pipe -- an editor adds its own
decoding and rendering on top (tests/perf/emacs_perf.sh measures that).

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
        src = ROOT / scenario["copyDir"]  # into an existing dir (3.6: no dirs_exist_ok)
        for entry in src.iterdir():
            if entry.is_dir():
                shutil.copytree(str(entry), str(Path(workdir) / entry.name))
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
    def __init__(self, cmd, stderr_path):
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

    def request(self, method, params, timeout=900):
        """(round-trip ms, response message, response bytes)."""
        rid = self.next_id
        self.next_id += 1
        t0 = time.perf_counter()
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        with self.cv:
            if not self.cv.wait_for(lambda: rid in self.responses or self.eof, timeout):
                sys.exit(f"lsp_perf: {method} timed out after {timeout} s")
            if self.eof and rid not in self.responses:
                sys.exit(f"lsp_perf: server exited during {method}")
            t1, msg, size = self.responses.pop(rid)
        if "error" in msg:
            sys.exit(f"lsp_perf: {method} failed: {msg['error']}")
        return (t1 - t0) * 1000, msg, size

    def wait_diagnostics(self, uri, version, since, timeout=900):
        """Time of the first diagnostics for (uri, version) after `since`."""
        def found():
            for t, p in self.diagnostics:
                if t >= since and p["uri"] == uri and p.get("version") == version:
                    return t
            return None
        with self.cv:
            if not self.cv.wait_for(lambda: found() is not None or self.eof, timeout):
                sys.exit(f"lsp_perf: no diagnostics for version {version} within {timeout} s")
            return found()


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
    client = Client([args.server, "--log-requests", str(reqlog_path)], work / "stderr.log")
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
    client.notify("initialized", {})
    version = 1
    t0 = time.perf_counter()
    client.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "systemverilog", "version": version, "text": text}})
    t = client.wait_diagnostics(uri, version, t0)
    phases["open -> first diagnostics"] = (t - t0) * 1000

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

    client.request("shutdown", None)
    client.notify("exit", None)
    try:
        client.proc.wait(10)
    except subprocess.TimeoutExpired:
        client.proc.kill()

    print_table(rows, budgets)
    if args.json:
        Path(args.json).write_text(json.dumps(
            {"scenario": scenario["name"], "server": args.server, "phases": phases,
             "rows": rows}, indent=2) + "\n")
    if not args.keep:
        shutil.rmtree(work)
    over = [r for r in rows if r["verdict"] in ("OVER BUDGET", "BREAKS FLOW")]
    return 1 if args.check and over else 0


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
    ap.add_argument("--json", help="also write the results here")
    ap.add_argument("--check", action="store_true", help="exit 1 if any p90 is over budget")
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
