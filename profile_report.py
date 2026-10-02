#!/usr/bin/env python3
"""Rime profiling report generator.

Usage:
  python3 profile_report.py profile.log > report.md
  python3 profile_report.py --threshold 2000 profile.log > report.md
  python3 profile_report.py --csv profile.csv profile.log
"""

import re
import sys
import argparse
from collections import defaultdict

PROFILE_RE = re.compile(r'\[PROFILE\] (\S+)\s+(.+?)\s+(\d+) ns')
KEY_RE = re.compile(r'Rime receive key: Key\((.+?)\)\s+(\d)')
TRANSLATOR_RE = re.compile(r'(.+?)(?:@(.+?))?\|s(\d+)l(\d+)')
# types ordered for display
TYPE_ORDER = [
    "engine", "processor", "translator", "filter",
    "segmentor", "menu", "post_processor", "formatter",
]
TYPE_CN = {
    "engine": "引擎阶段",
    "processor": "处理器",
    "translator": "翻译器",
    "filter": "过滤器",
    "segmentor": "分词器",
    "menu": "菜单",
    "post_processor": "后处理器",
    "formatter": "格式化器",
}
# thresholds in µs for spike detection
SPIKE_THRESHOLDS = {
    "engine": 50000,   # >50ms ProcessKey is a spike
    "processor": 500,  # >500µs processor call
    "translator": 5000, # >5ms translator call
    "filter": 2000,    # >2ms filter call
    "segmentor": 500,  # >500µs segmentor
    "menu": 200,       # >200µs menu prepare
}


def parse_translator(name):
    """Split 'script_translator@user_dict_set|s0l2' into parts."""
    m = TRANSLATOR_RE.match(name)
    if not m:
        return name, None, None, None
    klass = m.group(1)
    alias = m.group(2)
    seg = int(m.group(3))
    ilen = int(m.group(4))
    return klass, alias, seg, ilen


def is_lua(name):
    return name.startswith("*wanxiang") or name.startswith("*")


def collect(path):
    """Parse profile.log, return list of (type, name, ns) tuples."""
    rows = []
    with open(path) as f:
        for line in f:
            m = PROFILE_RE.match(line)
            if m:
                rows.append((m.group(1), m.group(2).strip(), int(m.group(3))))
    return rows


def aggregate(rows):
    """Return dict of (type, name) -> {count, sum_ns, max_ns, min_ns}."""
    agg = defaultdict(lambda: {"count": 0, "sum_ns": 0, "max_ns": 0, "min_ns": 2**63})
    for typ, name, ns in rows:
        d = agg[(typ, name)]
        d["count"] += 1
        d["sum_ns"] += ns
        if ns > d["max_ns"]:
            d["max_ns"] = ns
        if ns < d["min_ns"]:
            d["min_ns"] = ns
    return agg


def fmt_ns(ns, precision=1):
    """Format nanoseconds to human-readable."""
    ns = round(ns)
    if ns >= 1_000_000_000:
        return f"{ns/1_000_000_000:.{precision}f}s"
    if ns >= 1_000_000:
        return f"{ns/1_000_000:.{precision}f}ms"
    if ns >= 1_000:
        return f"{ns/1_000:.{precision}f}µs"
    return f"{ns}ns"


def fmt_table(headers, rows, align=None):
    """Format a Markdown table."""
    if not align:
        align = ["<"] * len(headers)
    lines = []
    lines.append("| " + " | ".join(headers) + " |")
    sep = []
    for i, h in enumerate(headers):
        a = align[i] if i < len(align) else "<"
        sep.append(":" + ("-" * (max(len(h), 1) + 1)) if a == "<" else
                   ("-" * (max(len(h), 1) + 1)) + ":" if a == ">" else
                   ":" + ("-" * max(len(h) - 1, 1)) + ":")
    lines.append("|" + "|".join(sep) + "|")
    for row in rows:
        lines.append("| " + " | ".join(str(c) for c in row) + " |")
    return "\n".join(lines)


def build_key_map(path):
    """Read raw lines, return dict of {line_index_of_ProcessKey: (key_name, is_press)}."""
    key_map = {}
    current_key = "?"
    current_press = True
    with open(path) as f:
        for i, line in enumerate(f):
            m = KEY_RE.search(line)
            if m:
                current_key = m.group(1)
                current_press = (m.group(2) == "0")
            elif '[PROFILE] engine' in line and 'ProcessKey' in line:
                key_map[i] = (current_key, current_press)
    return key_map


def generate_report(rows, key_map, args):
    """Generate the Markdown report."""
    agg = aggregate(rows)
    total_ns = sum(d["sum_ns"] for d in agg.values())

    # categorize by type
    by_type = defaultdict(list)
    for (typ, name), d in agg.items():
        by_type[typ].append((name, d))

    # extract ProcessKey values with keys
    pkeys_raw = []
    with open(args.logfile) as f:
        for i, line in enumerate(f):
            m = PROFILE_RE.match(line)
            if m and m.group(1) == "engine" and m.group(2).strip() == "ProcessKey":
                ki = key_map.get(i, ("?", True))
                pkeys_raw.append((int(m.group(3)), ki[0], ki[1]))
    pkeys = sorted(ns for ns, _, _ in pkeys_raw)

    lines = []
    lines.append("# Rime 性能分析报告")
    lines.append("")

    # ── 1. Summary ──
    lines.append("## 1. 总体统计")
    lines.append("")
    lines.append(f"| 指标 | 值 |")
    lines.append(f"|---|---|")
    lines.append(f"| Profile 记录行数 | {len(rows):,} |")
    lines.append(f"| 按键次数 | {len(pkeys):,} |")
    lines.append(f"| 总耗时 | {fmt_ns(total_ns)} |")
    lines.append("")
    lines.append(f"> 文档内的数值均从 profile.log 自动提取。")
    lines.append("")

    # ── 2. Latency distribution ──
    if pkeys:
        avg_ns = sum(pkeys) / len(pkeys)
        lines.append("## 2. 按键延迟分布 (ProcessKey)")
        lines.append("")
        lines.append(f"| 指标 | 值 |")
        lines.append(f"|---|---|")
        lines.append(f"| 采样次数 | {len(pkeys):,} |")
        lines.append(f"| 最小 | {fmt_ns(pkeys[0])} |")
        lines.append(f"| 中位数 (P50) | {fmt_ns(pkeys[len(pkeys)//2])} |")
        lines.append(f"| 平均 | {fmt_ns(avg_ns)} |")
        lines.append(f"| P95 | {fmt_ns(pkeys[int(len(pkeys)*0.95)])} |")
        lines.append(f"| P99 | {fmt_ns(pkeys[int(len(pkeys)*0.99)])} |")
        lines.append(f"| 最大 | {fmt_ns(pkeys[-1])} |")
        extreme = [p for p in pkeys if p > 50_000_000]  # >50ms
        if extreme:
            lines.append(f"| >50ms 次数 | {len(extreme)} ({len(extreme)/len(pkeys)*100:.1f}%) |")
        lines.append("")

    # ── 3. Phase markers ──
    phase_names = ["ProcessKey", "Compose", "TransSeg", "CalcSeg"]
    phase_data = [(n, d) for (n, d) in by_type["engine"] if n in phase_names]
    if phase_data:
        lines.append("## 3. Phase 标记")
        lines.append("")
        rows_t = []
        for name, d in sorted(phase_data, key=lambda x: -x[1]["sum_ns"]):
            rows_t.append([
                f"`{name}`",
                d["count"],
                fmt_ns(d["sum_ns"] / d["count"]),
                fmt_ns(d["sum_ns"]),
                fmt_ns(d["max_ns"]),
            ])
        lines.append(fmt_table(["组件", "次数", "平均", "总计", "最大"], rows_t))
        lines.append("")

    # ── 4. Per-type tables ──
    lines.append("## 4. 各类型组件统计")
    lines.append("")

    for typ in TYPE_ORDER:
        if typ not in by_type:
            continue
        items = sorted(by_type[typ], key=lambda x: -x[1]["sum_ns"])
        tp = by_type[typ]
        type_total_ns = sum(d["sum_ns"] for _, d in tp)
        type_pct = type_total_ns / total_ns * 100 if total_ns else 0
        cn = TYPE_CN.get(typ, typ)

        lines.append(f"### 4.{TYPE_ORDER.index(typ)+1} {cn} (`{typ}`)")
        lines.append(f"总计 {fmt_ns(type_total_ns)}，占总体 {type_pct:.1f}%")
        lines.append("")

        if typ == "translator":
            headers = ["组件", "别名", "Seg", "长度", "次数", "平均", "最大", "总计", "类型"]
            rows_t = []
            for name, d in items:
                klass, alias, seg, ilen = parse_translator(name)
                lua = "Lua" if is_lua(name) else "C++"
                rows_t.append([
                    klass,
                    alias or "-",
                    seg if seg is not None else "-",
                    ilen if ilen is not None else "-",
                    d["count"],
                    fmt_ns(d["sum_ns"] / d["count"]),
                    fmt_ns(d["max_ns"]),
                    fmt_ns(d["sum_ns"]),
                    lua,
                ])
            lines.append(fmt_table(headers, rows_t))
        else:
            headers = ["组件", "次数", "平均", "最大", "总计", "类型"]
            rows_t = []
            for name, d in items:
                display = name if name else "(匿名)"
                lua = "Lua" if is_lua(name) else "C++"
                rows_t.append([
                    f"`{display}`",
                    d["count"],
                    fmt_ns(d["sum_ns"] / d["count"]),
                    fmt_ns(d["max_ns"]),
                    fmt_ns(d["sum_ns"]),
                    lua,
                ])
            lines.append(fmt_table(headers, rows_t))
        lines.append("")

    # ── 5. Lua vs C++ ──
    lines.append("## 5. Lua vs C++ 对比")
    lines.append("")
    lua_ns = defaultdict(int)
    cpp_ns = defaultdict(int)
    for typ in ["processor", "segmentor", "translator", "filter"]:
        for name, d in by_type.get(typ, []):
            if is_lua(name):
                lua_ns[typ] += d["sum_ns"]
            else:
                cpp_ns[typ] += d["sum_ns"]
    headers = ["类别", "Lua 总计", "C++ 总计", "Lua 占比"]
    rows_l = []
    for typ in ["processor", "translator", "filter", "segmentor"]:
        lt = lua_ns[typ]
        ct = cpp_ns[typ]
        if lt + ct == 0:
            continue
        pct = lt / (lt + ct) * 100
        rows_l.append([TYPE_CN.get(typ, typ), fmt_ns(lt), fmt_ns(ct), f"{pct:.1f}%"])
    total_lua = sum(lua_ns.values())
    total_cpp = sum(cpp_ns.values())
    if total_lua + total_cpp > 0:
        rows_l.append([
            "**合计**",
            f"**{fmt_ns(total_lua)}**",
            f"**{fmt_ns(total_cpp)}**",
            f"**{total_lua/(total_lua+total_cpp)*100:.1f}%**",
        ])
    lines.append(fmt_table(headers, rows_l))
    lines.append("")

    # ── 6. Spike detection ──
    lines.append("## 6. 尖峰分析")
    lines.append("")
    lines.append(f"> 阈值: processor>{SPIKE_THRESHOLDS['processor']}µs, "
                 f"translator>{SPIKE_THRESHOLDS['translator']}µs, "
                 f"filter>{SPIKE_THRESHOLDS['filter']}µs, "
                 f"engine>{SPIKE_THRESHOLDS['engine']}µs")
    lines.append("")

    spike_count = 0
    for typ in ["processor", "translator", "filter", "segmentor", "menu"]:
        if typ not in by_type:
            continue
        threshold_ns = SPIKE_THRESHOLDS.get(typ, 1000) * 1000
        spikes = []
        for name, d in by_type[typ]:
            if d["max_ns"] > threshold_ns:
                spikes.append((name, d["max_ns"], d["sum_ns"] / d["count"]))
        if spikes:
            spikes.sort(key=lambda x: -x[1])
            spike_count += len(spikes)
            cn = TYPE_CN.get(typ, typ)
            lines.append(f"### {cn} 尖峰")
            lines.append("")
            lines.append(fmt_table(
                ["组件", "最大", "平均", "倍数"],
                [(f"`{n}`", fmt_ns(mx), fmt_ns(avg), f"{mx/avg:.0f}x")
                 for n, mx, avg in spikes[:10]],
            ))
            lines.append("")

    if spike_count == 0:
        lines.append("未检测到尖峰。")
        lines.append("")

    # ── 7. Top slow keys ──
    if pkeys:
        lines.append("## 7. 最慢按键 Top 10")
        lines.append("")
        top_slow = sorted(pkeys_raw, key=lambda x: -x[0])[:10]
        lines.append(fmt_table(
            ["排名", "延迟", "按键", "事件"],
            [(i + 1, fmt_ns(ns), f"`{key}`",
              "按下" if press else "释放")
             for i, (ns, key, press) in enumerate(top_slow)],
        ))
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Rime profiling report generator")
    parser.add_argument("logfile", help="Path to profile.log")
    parser.add_argument("--threshold", type=int, default=None,
                        help="Override spike threshold (µs) for all components")
    parser.add_argument("--csv", metavar="PATH",
                        help="Also export raw aggregated data as CSV")
    args = parser.parse_args()

    if args.threshold:
        for k in SPIKE_THRESHOLDS:
            SPIKE_THRESHOLDS[k] = args.threshold

    rows = collect(args.logfile)
    if not rows:
        print("Error: no [PROFILE] lines found in", args.logfile, file=sys.stderr)
        sys.exit(1)

    key_map = build_key_map(args.logfile)
    report = generate_report(rows, key_map, args)
    print(report)

    if args.csv:
        agg = aggregate(rows)
        with open(args.csv, "w") as f:
            f.write("type,name,count,avg_ns,total_ns,max_ns,min_ns\n")
            for (typ, name), d in agg.items():
                avg = d["sum_ns"] / d["count"]
                name_escaped = name.replace('"', '""')
                f.write(f'{typ},"{name_escaped}",{d["count"]},{avg:.0f},{d["sum_ns"]},{d["max_ns"]},{d["min_ns"]}\n')
        print(f"\nCSV exported to {args.csv}", file=sys.stderr)


if __name__ == "__main__":
    main()
