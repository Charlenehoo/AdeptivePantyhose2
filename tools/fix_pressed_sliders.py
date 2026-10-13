# tools/fix_pressed_sliders.py
"""
把 XML / OSP 文件中所有形如：
    <Slider name="Pressed" invert="false" small="任意" big="任意">
替换为：
    <Slider name="Pressed" invert="false" small="0" big="0">

保留原有的缩进、属性顺序（只在 name="Pressed" 上生效）。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# 匹配 <Slider name="Pressed" ... > 这一行，只针对 name="Pressed"
# 允许属性顺序为 invert / small / big，并允许有其它属性在中间
SLIDER_PRESSED_RE = re.compile(
    r'(<Slider\s+name="Pressed"[^>]*?)'
    r'\bsmall="[^"]*"'
    r"([^>]*?)"
    r'\bbig="[^"]*"'
    r"([^>]*?>)",
    re.IGNORECASE,
)

REPLACEMENT = r'\1small="0"\2big="0"\3'


def process_file(path: Path, dry_run: bool = False) -> int:
    """处理单个文件，返回替换次数。"""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        # 兜底：有些工具生成的 osp 可能不是 utf-8
        text = path.read_text(encoding="utf-8-sig")

    new_text, n = SLIDER_PRESSED_RE.subn(REPLACEMENT, text)

    if n == 0:
        return 0

    if dry_run:
        print(f"[DRY-RUN] {path}  -> 将替换 {n} 处")
        # 打印一下修改前后的行，方便确认
        for line in text.splitlines():
            if 'name="Pressed"' in line:
                old = line.strip()
                new_line = SLIDER_PRESSED_RE.sub(REPLACEMENT, line)
                print(f"    - {old}")
                print(f"    + {new_line.strip()}")
    else:
        path.write_text(new_text, encoding="utf-8")
        print(f"[OK] {path}  -> 替换 {n} 处")

    return n


def iter_targets(root: Path, recursive: bool) -> list[Path]:
    """收集 .xml 和 .osp 文件。"""
    if root.is_file():
        return [root]

    if recursive:
        files = list(root.rglob("*.xml")) + list(root.rglob("*.osp"))
    else:
        files = list(root.glob("*.xml")) + list(root.glob("*.osp"))

    # 去重 + 排序
    return sorted({p.resolve() for p in files})


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description='把 name="Pressed" 的 Slider 的 small / big 全部改为 0。'
    )
    parser.add_argument(
        "path",
        nargs="?",
        default=r"assets\CalienteTools\BodySlide\SliderSets",
        help="要处理的文件或目录（默认: assets\\CalienteTools\\BodySlide\\SliderSets）",
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="只显示将要修改的内容，不写入文件",
    )
    parser.add_argument(
        "-r",
        "--recursive",
        action="store_true",
        help="递归处理子目录（默认只处理当前目录）",
    )

    args = parser.parse_args(argv)

    root = Path(args.path)
    if not root.exists():
        print(f"路径不存在: {root}", file=sys.stderr)
        return 1

    targets = iter_targets(root, args.recursive)
    if not targets:
        print(f"未找到 .xml / .osp 文件: {root}", file=sys.stderr)
        return 1

    total_files = 0
    total_subs = 0
    for f in targets:
        n = process_file(f, dry_run=args.dry_run)
        if n:
            total_files += 1
            total_subs += n

    print()
    print(f"完成。共修改 {total_files} 个文件，{total_subs} 处 Slider。")
    if args.dry_run:
        print("（这是 dry-run，未写入磁盘）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
