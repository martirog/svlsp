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
while [ $# -gt 0 ]; do
    case "$1" in
        -i) interval="$2"; shift 2 ;;
        -n) count="$2"; shift 2 ;;
        -o) out="$2"; shift 2 ;;
        -p) onlypid="$2"; shift 2 ;;
        --no-gdb) usegdb=0; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ "$usegdb" = 1 ] && ! command -v gdb >/dev/null 2>&1; then
    echo "gdb not found: recording without stack traces" >&2
    usegdb=0
fi

hz=$(getconf CLK_TCK)
declare -A prev   # "pid:tid" -> utime+stime ticks at the last sample

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
        ps -o pid=,ppid=,etime=,time=,pcpu=,rss=,nlwp=,args= -p "$pid" |
            awk '{printf "pid %s ppid %s elapsed %s cpu %s %%cpu %s rss %d MB threads %s cmd:", $1,$2,$3,$4,$5,$6/1024,$7; for(i=8;i<=NF;i++) printf " %s",$i; print ""}'
        echo "threads (cpu% since last sample, state, wchan):"
        local task tid stat rest st ticks key d pct wchan
        for task in /proc/"$pid"/task/*; do
            tid=${task##*/}
            stat=$(cat "$task/stat" 2>/dev/null) || continue
            rest=${stat##*) }          # fields after "(comm) "
            set -- $rest
            st=$1; ticks=$(( ${12} + ${13} ))
            key="$pid:$tid"
            if [ -n "${prev[$key]:-}" ]; then
                d=$(( ticks - prev[$key] ))
                pct=$(awk -v d="$d" -v hz="$hz" -v i="$interval" 'BEGIN{printf "%5.1f", 100*d/hz/i}')
            else
                pct="  new"
            fi
            prev[$key]=$ticks
            wchan=$(cat "$task/wchan" 2>/dev/null); [ -n "$wchan" ] || wchan=-
            printf "  tid %-8s cpu %s%%  state %s  total %ss  wchan %s\n" \
                "$tid" "$pct" "$st" "$(( ticks / hz ))" "$wchan"
        done
        if [ "$usegdb" = 1 ]; then
            echo "stacks:"
            timeout 60 gdb -p "$pid" -batch -nx -q \
                -ex 'set pagination off' -ex 'set print thread-events off' \
                -ex 'info threads' -ex 'thread apply all bt 40' 2>&1 |
                grep -v '^\[New LWP\|^Using host libthread_db\|^\[Thread debugging'
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
