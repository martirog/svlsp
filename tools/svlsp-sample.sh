#!/usr/bin/env bash
# svlsp-sample.sh -- record what running svlsp processes are doing, at an
# interval, into one log file to bring back for debugging.
#
# Usage: tools/svlsp-sample.sh [-i SECONDS] [-n COUNT] [-o FILE] [-p PID] [--no-gdb]
#   -i  seconds between samples (default 60)
#   -n  number of samples (default 0 = until Ctrl-C)
#   -o  log file (default ./svlsp-sample-<host>-<date>.log)
#   -p  sample only this PID (default: every process named svlsp of this user)
#   --no-gdb  skip the stack traces (only CPU / memory / thread states)
#   -t  seconds gdb may take per sample (default 300; an old gdb reading
#       GCC 14 libraries can take minutes)
# Environment: GDB=/path/to/gdb picks the gdb (default: the one on PATH;
# a newer one, e.g. a conda env's, reads DWARF 5 far faster than gdb 7.x).
#
# Each sample has, per svlsp process (daemon, parse worker, relay):
#   - ps line: elapsed time, total CPU time, %CPU, RSS, thread count, command
#   - per thread: CPU used since the last sample (%), state, kernel wait channel
#   - a gdb backtrace of every thread (function names; the release build is
#     not stripped). gdb stops the process for well under a second.
# Needs only bash, ps, /proc and (for stacks) gdb. If gdb says "ptrace:
# Operation not permitted", run it with sudo, or
#   sudo sysctl kernel.yama.ptrace_scope=0
set -u

interval=60
count=0
out="svlsp-sample-$(hostname -s)-$(date +%Y%m%d-%H%M%S).log"
onlypid=""
usegdb=1
gdbtimeout=300
gdb=${GDB:-gdb}
while [ $# -gt 0 ]; do
    case "$1" in
        -i) interval="$2"; shift 2 ;;
        -n) count="$2"; shift 2 ;;
        -o) out="$2"; shift 2 ;;
        -p) onlypid="$2"; shift 2 ;;
        -t) gdbtimeout="$2"; shift 2 ;;
        --no-gdb) usegdb=0; shift ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ "$usegdb" = 1 ] && ! command -v "$gdb" >/dev/null 2>&1; then
    echo "$gdb not found: recording without stack traces" >&2
    usegdb=0
fi

hz=$(getconf CLK_TCK)
declare -A prev   # "pid:tid" -> utime+stime ticks at the last sample
declare -A prevt  # "pid:tid" -> uptime (s) at the last sample

uptime_s() { local u; read -r u _ </proc/uptime; echo "$u"; }

# Stacks of every thread. Shared libraries' debug info is skipped
# (debug-file-directory): their own symbol tables still name the frames,
# and reading the separate debug files is what makes an old gdb slow.
# If that still prints no thread, retry without loading the libraries at
# all: svlsp's own frames stay named.
stacks() {
    local o
    o=$(timeout "$gdbtimeout" "$gdb" -p "$1" -batch -nx -q \
            -iex 'set debug-file-directory /nonexistent' \
            -ex 'set pagination off' -ex 'set print thread-events off' \
            -ex 'info threads' -ex 'thread apply all bt 40' 2>&1)
    if ! grep -q '^Thread ' <<<"$o"; then
        echo "(gdb printed no stacks, exit/timeout; retrying without shared libraries)"
        o=$(timeout "$gdbtimeout" "$gdb" -p "$1" -batch -nx -q \
                -iex 'set auto-solib-add off' \
                -ex 'set pagination off' -ex 'info threads' \
                -ex 'thread apply all bt 40' 2>&1)
    fi
    grep -v '^\[New LWP\|^Using host libthread_db\|^\[Thread debugging\|^Dwarf Error\|debug information found\|^$' <<<"$o"
}

pids() {
    if [ -n "$onlypid" ]; then echo "$onlypid"; else pgrep -u "$(id -u)" -x svlsp; fi
}

sample() {
    echo "===== sample $1 at $(date '+%Y-%m-%d %H:%M:%S') ====="
    local any=0 pid
    for pid in $(pids); do
        [ -d "/proc/$pid" ] || continue
        any=1
        echo "--- pid $pid"
        # one -o per field: procps 3.2 (CentOS 7) reads "pid=,ppid=,..." as one header
        ps -o pid= -o ppid= -o etime= -o time= -o pcpu= -o rss= -o nlwp= -o args= -p "$pid" |
            awk '{printf "pid %s ppid %s elapsed %s cpu %s %%cpu %s rss %d MB threads %s cmd:", $1,$2,$3,$4,$5,$6/1024,$7; for(i=8;i<=NF;i++) printf " %s",$i; print ""}'
        echo "threads (cpu% since last sample, state, wchan):"
        local task tid stat rest st ticks key d pct wchan now
        now=$(uptime_s)
        for task in /proc/"$pid"/task/*; do
            tid=${task##*/}
            stat=$(cat "$task/stat" 2>/dev/null) || continue
            rest=${stat##*) }          # fields after "(comm) "
            set -- $rest
            st=$1; ticks=$(( ${12} + ${13} ))
            key="$pid:$tid"
            if [ -n "${prev[$key]:-}" ]; then
                d=$(( ticks - prev[$key] ))
                pct=$(awk -v d="$d" -v hz="$hz" -v t0="${prevt[$key]}" -v t1="$now" \
                      'BEGIN{i=t1-t0; if (i<=0) i=1; printf "%5.1f", 100*d/hz/i}')
            else
                pct="  new"
            fi
            prev[$key]=$ticks; prevt[$key]=$now
            wchan=$(cat "$task/wchan" 2>/dev/null); [ -n "$wchan" ] || wchan=-
            printf "  tid %-8s cpu %s%%  state %s  total %ss  wchan %s\n" \
                "$tid" "$pct" "$st" "$(( ticks / hz ))" "$wchan"
        done
        if [ "$usegdb" = 1 ]; then
            echo "stacks:"
            local t0=$SECONDS
            stacks "$pid"
            echo "(gdb took $((SECONDS - t0)) s; the process is stopped meanwhile)"
        fi
    done
    [ "$any" = 1 ] || echo "(no svlsp process found)"
    echo
}

echo "writing to $out every ${interval}s (Ctrl-C to stop)" >&2
{
    echo "svlsp-sample on $(hostname) $(uname -r), user $(id -un), interval ${interval}s"
    for pid in $(pids); do
        exe=$(readlink "/proc/$pid/exe" 2>/dev/null)
        echo "pid $pid exe $exe version: $("$exe" --version 2>/dev/null | head -1)"
    done
    echo
} >>"$out"

i=1
trap 'echo "stopped after $((i-1)) samples; log: $out" >&2; exit 0' INT TERM
while :; do
    sample "$i" >>"$out" 2>&1
    echo "sample $i written" >&2
    [ "$count" -gt 0 ] && [ "$i" -ge "$count" ] && break
    i=$((i + 1))
    sleep "$interval"
done
echo "done: $out" >&2
