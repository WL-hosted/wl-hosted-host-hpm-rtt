#!/usr/bin/env python3
"""
gen_part_table.py — 由 part_table.md 生成 FAL 分区表头（part_table.h）与分区图

用法:
    python3 gen_part_table.py <part_table.md> <part_table.h> [part_table_map.png]

输入 md 表格格式（首行为表头，第二行为分隔行，可省略）:
    | name | flash_name | offset | len |
    | --- | --- | --- | --- |
    | bl | onchip | 0 | 128k |
    | app | onchip | 128k | 3456k |
    | app_s2 | norflash0 | 0 | 4096k |
    | fs | norflash0 | 8192k | 8192k |

偏移/大小支持 k/K（KiB）与 m/M（MiB）后缀，输出为 C 表达式（如 128*1024）。
flash_name 列区分不同 flash 设备（FAL 按 flash_name 挂载分区），
不同 flash 的分区各自从偏移 0 开始，互不影响。

输出:
    part_table.h        包含 FAL_PART_TABLE 宏（fal_cfg.h 经 #include 引入，
                        flash 设备名取 md 的 flash_name 列字面量，如 "onchip"）
    part_table_map.png  可选：分区布局 PNG 图（matplotlib 无界面绘制；未安装或
                        绘图失败时仅告警跳过，不影响构建）

校验: 同一 flash 内分区互不重叠、长度非零（不要求连续、无空洞），
不同 flash 之间不做连续性/重叠判断，否则报错退出。
"""

import argparse
import re
import sys

SIZE_RE = re.compile(r"^(\d+)\s*([kKmM]?)$")


def parse_size(text, where):
    m = SIZE_RE.match(text.strip())
    if not m:
        raise ValueError(f"{where}: 无法解析大小/偏移: {text!r}")
    n = int(m.group(1))
    unit = m.group(2).lower()
    if unit == "k":
        expr = f"{n}*1024"
        size = n * 1024
    elif unit == "m":
        expr = f"{n}*1024*1024"
        size = n * 1024 * 1024
    else:
        expr = f"{n}"
        size = n
    return size, expr


def parse_table(md_path):
    rows = []
    with open(md_path, "r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < 4:
                continue
            # 分隔行（|---| 等）跳过
            if all(re.fullmatch(r":?-+:?", c) for c in cells):
                continue
            name, flash_name, offset_text, len_text = cells[0], cells[1], cells[2], cells[3]
            if name in ("name", "Name") and flash_name in ("flash_name", "flash name"):
                continue  # 表头行
            off_size, off_expr = parse_size(offset_text, f"{md_path}:{lineno}")
            len_size, len_expr = parse_size(len_text, f"{md_path}:{lineno}")
            if len_size <= 0:
                raise ValueError(f"{md_path}:{lineno}: 分区长度必须大于 0: {len_text!r}")
            rows.append(
                {
                    "name": name,
                    "flash_name": flash_name,
                    "offset": off_size,
                    "offset_expr": off_expr,
                    "len": len_size,
                    "len_expr": len_expr,
                }
            )
    if not rows:
        raise ValueError(f"{md_path}: 未解析到任何分区行")
    return rows


def group_by_flash(rows):
    by_flash = {}
    for r in rows:
        by_flash.setdefault(r["flash_name"], []).append(r)
    return by_flash


def validate(rows):
    """按 flash 分组校验：同一 flash 内分区互不重叠（不要求连续/无空洞）。"""
    for flash_name, parts in group_by_flash(rows).items():
        parts = sorted(parts, key=lambda p: p["offset"])
        for prev, cur in zip(parts, parts[1:]):
            if cur["offset"] < prev["offset"] + prev["len"]:
                raise ValueError(
                    f"分区 {prev['name']!r} 与 {cur['name']!r} 在 flash {flash_name!r} 上重叠："
                    f"{prev['name']} 范围 [0x{prev['offset']:x}, +{prev['len_expr']})，"
                    f"{cur['name']} 偏移 {cur['offset_expr']}"
                )


def flash_spans(rows):
    """每颗 flash 覆盖的最大地址（offset+len 的最大值，字节）。"""
    spans = {}
    for r in rows:
        end = r["offset"] + r["len"]
        if spans.get(r["flash_name"], 0) < end:
            spans[r["flash_name"]] = end
    return spans


def human_size(n):
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):g} MiB"
    if n >= 1024:
        return f"{n / 1024:g} KiB"
    return f"{n} B"


def render(rows, spans, md_path, out_path):
    name_w = max(len(r["name"]) for r in rows)
    flash_w = max(len(r["flash_name"]) for r in rows)
    off_w = max(len(r["offset_expr"]) for r in rows)
    len_w = max(len(r["len_expr"]) for r in rows)

    layout = ", ".join(f"{f}: {human_size(s)}" for f, s in spans.items())

    lines = [
        "/*",
        " * part_table.h — 生成文件，请勿手改（DO NOT EDIT）。",
        f" * 来源: {md_path}",
        f" * 生成器: tools/gen_part_table.py",
        f" * 分区布局: {layout}",
        " */",
        "",
        "#ifndef PART_TABLE_H",
        "#define PART_TABLE_H",
        "",
        "/* FAL 分区表（BL 落盘分区表，由 part_table.md 生成，经 fal_cfg.h 引入） */",
        "#define FAL_PART_TABLE                                                          \\",
        "    {                                                                           \\",
    ]
    for r in rows:
        cell = (
            "{FAL_PART_MAGIC_WORD, "
            + f'"{r["name"]}"'.ljust(name_w + 2)
            + ", "
            + f'"{r["flash_name"]}"'.ljust(flash_w + 2)
            + ", "
            + r["offset_expr"].ljust(off_w)
            + ", "
            + r["len_expr"].ljust(len_w)
            + ", 0},"
        )
        lines.append("        " + cell + " \\")
    lines += [
        "    }",
        "",
        "#endif /* PART_TABLE_H */",
        "",
    ]
    return "\n".join(lines)


def render_map_png(rows, spans, out_png):
    """用 matplotlib 绘制分区布局图并导出 PNG（无界面 Agg 后端）。

    每颗 flash 一张横向条图：分区按实际偏移/长度绘制矩形，颜色区分，
    名称/大小标在条内或条上方，范围标在条下方，未分配区域留空。
    """
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    from matplotlib.patches import Rectangle
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    # 优先使用中文字体，避免中文标签显示为方块
    cjk_candidates = [
        "PingFang SC", "Hiragino Sans GB", "Heiti SC", "Songti SC",
        "Noto Sans CJK SC", "Source Han Sans SC", "WenQuanYi Micro Hei",
        "Microsoft YaHei", "SimHei",
    ]
    available = {f.name for f in font_manager.fontManager.ttflist}
    cjk_font = next((n for n in cjk_candidates if n in available), None)
    if cjk_font:
        plt.rcParams["font.sans-serif"] = [cjk_font]
    plt.rcParams["axes.unicode_minus"] = False

    by_flash = group_by_flash(rows)
    n_flash = len(by_flash)
    fig, axes = plt.subplots(n_flash, 1, figsize=(10, max(2.0 * n_flash, 2.5)), squeeze=False)
    colors = plt.get_cmap("tab10").colors

    for idx, (flash_name, parts) in enumerate(by_flash.items()):
        ax = axes[idx][0]
        parts = sorted(parts, key=lambda p: p["offset"])
        span = spans[flash_name]

        # flash 占用范围底色（灰），未分配区域自然留空
        ax.add_patch(
            Rectangle(
                (0, -0.4), span, 0.8,
                facecolor="#eeeeee", edgecolor="black", linewidth=1.0,
            )
        )
        for i, p in enumerate(parts):
            ax.add_patch(
                Rectangle(
                    (p["offset"], -0.4), p["len"], 0.8,
                    facecolor=colors[i % len(colors)], edgecolor="black",
                    linewidth=1.0, alpha=0.9,
                )
            )
            fraction = p["len"] / span
            if cjk_font:
                label = f"{p['name']}（{human_size(p['len'])}）"
            else:
                label = f"{p['name']} {human_size(p['len'])}"
            cx = p["offset"] + p["len"] / 2
            if fraction > 0.10:
                ax.text(cx, 0.0, label, ha="center", va="center", fontsize=9)
            else:
                # 分区过窄时把完整标签放到条上方；靠边时改用左右对齐防止溢出图区
                if cx < span * 0.10:
                    ha, tx = "left", cx
                elif cx > span * 0.90:
                    ha, tx = "right", cx
                else:
                    ha, tx = "center", cx
                ax.text(tx, 0.62, label, ha=ha, va="bottom", fontsize=8)
            rng = f"0x{p['offset']:x}-0x{p['offset'] + p['len']:x}"
            if fraction > 0.06:
                ax.text(cx, -0.62, rng, ha="center", va="top", fontsize=7)

        if cjk_font:
            title = f"[{flash_name}] 占用范围 0x0-0x{span:x}（{human_size(span)}，{len(parts)} 个分区）"
            xlabel = "flash 内偏移（字节）"
        else:
            title = f"[{flash_name}] range 0x0-0x{span:x} ({human_size(span)}, {len(parts)} parts)"
            xlabel = "offset in flash (bytes)"
        ax.set_title(title, fontsize=10)
        ax.set_xlim(0, span)
        ax.set_ylim(-0.85, 0.85)
        ax.set_yticks([])
        ax.xaxis.set_major_locator(MaxNLocator(5))
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v, pos: f"0x{int(v):x}"))
        ax.grid(axis="x", linestyle=":", alpha=0.6)
        for spine in ("top", "right", "left"):
            ax.spines[spine].set_visible(False)

    fig.supxlabel(xlabel, fontsize=10)
    fig.tight_layout()
    fig.savefig(out_png, dpi=150)
    plt.close(fig)


def try_render_map_png(rows, spans, out_png):
    """导出 PNG 分区图；matplotlib 不可用或绘图失败时仅告警跳过，不影响构建。"""
    try:
        import matplotlib  # noqa: F401
    except ImportError:
        print(
            f"gen_part_table: 警告: 未安装 matplotlib，跳过 PNG 分区图导出: {out_png}",
            file=sys.stderr,
        )
        return
    try:
        render_map_png(rows, spans, out_png)
    except Exception as e:  # 分区图仅为辅助产物，失败不阻断构建
        print(f"gen_part_table: 警告: PNG 分区图导出失败，已跳过: {e}", file=sys.stderr)
        return
    print(f"gen_part_table: 已生成分区图 {out_png}")


def main():
    parser = argparse.ArgumentParser(description="由 part_table.md 生成 FAL 分区表头与分区图")
    parser.add_argument("md", help="输入 part_table.md 路径")
    parser.add_argument("out", help="输出 part_table.h 路径")
    parser.add_argument("map_png", nargs="?", default=None, help="可选：输出分区图 PNG 路径（matplotlib，未安装则跳过）")
    args = parser.parse_args()

    try:
        rows = parse_table(args.md)
        validate(rows)
        spans = flash_spans(rows)
        content = render(rows, spans, args.md, args.out)
    except ValueError as e:
        print(f"gen_part_table: 错误: {e}", file=sys.stderr)
        sys.exit(1)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(content)
    summary = ", ".join(f"{f}: {human_size(s)}" for f, s in spans.items())
    print(f"gen_part_table: 已生成 {args.out}（{len(rows)} 个分区；{summary}）")

    if args.map_png:
        try_render_map_png(rows, spans, args.map_png)


if __name__ == "__main__":
    main()
