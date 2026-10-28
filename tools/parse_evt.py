import argparse
import re
import statistics
import sys

EVT_RE = re.compile(r"^EVT t_us=(\d+) name=(\S+)\s*$")
STAT_RE = re.compile(r"^STAT name=(\S+)\s+n=(\d+)\s+min_us=(\d+)\s+avg_us=(\d+)\s+max_us=(\d+)\s*$")
JITTER_RE = re.compile(r"^JITTER name=(\S+)\s+period_ms=(\d+)\s+min_us=(-?\d+)\s+max_us=(-?\d+)\s+missed=(\d+)\s*$")
LOAD_RE = re.compile(r"^LOAD pct=(\d+)\s+avg=(\d+)\s+max=(\d+)\s+n=(\d+)\s*$")
STACK_RE = re.compile(r"^STACK reserved=(\d+)\s+used=(\d+)\s+free=(\d+)\s*$")
FW_RE = re.compile(r"METRICS fw=(\S+) clk=(\d+) Hz")

DEFAULT_PAIRS = [
    ("btn_press", "btn_react"),
    ("cmd_rx", "cmd_done"),
    ("capture_start", "capture_done"),
    ("flash_start", "flash_done"),
]

U32 = 1 << 32

def parse_log(path):
    events, stats, jitters, loads, stacks, fw = [], [], [], [], [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if fw is None:
                m = FW_RE.search(line)
                if m:
                    fw = (m.group(1), m.group(2))
            m = EVT_RE.match(line)
            if m:
                events.append((int(m.group(1)), m.group(2)))
                continue
            m = STAT_RE.match(line)
            if m:
                stats.append({
                    "name": m.group(1), "n": int(m.group(2)),
                    "min_us": int(m.group(3)), "avg_us": int(m.group(4)),
                    "max_us": int(m.group(5)),
                })
                continue
            m = JITTER_RE.match(line)
            if m:
                jitters.append({
                    "name": m.group(1), "period_ms": int(m.group(2)),
                    "min_us": int(m.group(3)), "max_us": int(m.group(4)),
                    "missed": int(m.group(5)),
                })
                continue
            m = LOAD_RE.match(line)
            if m:
                loads.append({
                    "pct": int(m.group(1)), "avg": int(m.group(2)),
                    "max": int(m.group(3)), "n": int(m.group(4)),
                })
                continue
            m = STACK_RE.match(line)
            if m:
                stacks.append({
                    "reserved": int(m.group(1)), "used": int(m.group(2)),
                    "free": int(m.group(3)),
                })
    return fw, events, stats, jitters, loads, stacks

def unwrap(events):
    if not events:
        return []
    out, acc, prev = [], 0, events[0][0]
    for t, name in events:
        if t < prev - (U32 // 2):
            acc += U32
        out.append((t + acc, name))
        prev = t
    return out

def latency(events, a, b):
    out, t0 = [], None
    for t, name in events:
        if name == a:
            t0 = t
        elif name == b and t0 is not None:
            out.append(t - t0)
            t0 = None
    return out

def fmt(d):
    if not d:
        return "n=0"
    return "n=%d min=%d median=%d avg=%d max=%d us" % (
        len(d), min(d), int(statistics.median(d)),
        int(statistics.mean(d)), max(d))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--pair", action="append", default=[])
    args = ap.parse_args()

    fw, events, stats, jitters, loads, stacks = parse_log(args.log)

    if fw:
        print("firmware: %s  clk: %s Hz" % (fw[0], fw[1]))
    if not events and not stats:
        print("no EVT/STAT lines found", file=sys.stderr)
        return 1

    pairs = []
    for p in args.pair:
        try:
            a, b = p.split(",")
            pairs.append((a.strip(), b.strip()))
        except ValueError:
            print("--pair expects start,end got %s" % p, file=sys.stderr)
            return 1
    if not pairs:
        pairs = DEFAULT_PAIRS

    print("\n== delays ==")
    events = unwrap(events)
    for a, b in pairs:
        d = latency(events, a, b)
        print("  %s -> %s: %s" % (a, b, fmt(d)))

    if stats:
        print("\n== STAT ==")
        print("  %s %s %s %s %s" % ("name", "n", "min_us", "avg_us", "max_us"))
        for s in stats:
            print("  %-12s %8d %9d %9d %9d" % (
                s["name"], s["n"], s["min_us"], s["avg_us"], s["max_us"]))

    if jitters:
        print("\n== JITTER ==")
        print("  %s %s %s %s %s" % ("name", "period_ms", "min_us", "max_us", "missed"))
        for j in jitters:
            print("  %-12s %9d %9d %9d %7d" % (
                j["name"], j["period_ms"], j["min_us"], j["max_us"], j["missed"]))

    if loads:
        print("\n== LOAD ==")
        worst = max(loads, key=lambda l: l["pct"])
        print("  count=%d pct(max)=%d avg=%d max=%d" % (
            len(loads), worst["pct"],
            max(loads, key=lambda l: l["avg"])["avg"], worst["max"]))

    if stacks:
        s = stacks[-1]
        print("\n== STACK ==")
        print("  reserved=%d used=%d free=%d" % (s["reserved"], s["used"], s["free"]))
        if s["free"] == 0:
            print("  warning: stack may be exhausted")

    return 0

if __name__ == "__main__":
    sys.exit(main())