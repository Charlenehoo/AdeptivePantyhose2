"""命令行入口。

用法：
    git ls-files | python -m tools.scan
    git ls-files | python -m tools.scan output.md
"""

from __future__ import annotations

import sys
from pathlib import Path

from .file_reader import FileReader
from .ignore_rules import IgnoreRules
from .lang_map import LanguageMap
from .path_source import StdinPathSource
from .renderer import MarkdownRenderer
from .scanner import Scanner

LANG_CONFIG_PATH = Path(__file__).with_name("languages.json")
IGNORE_FILE_NAME = ".scanignore"


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    out_path = Path(argv[0]) if argv else Path("scan.md")

    scanner = Scanner(
        source=StdinPathSource(sys.stdin),
        ignore=IgnoreRules.from_file(Path(IGNORE_FILE_NAME)),
        languages=LanguageMap.from_json(LANG_CONFIG_PATH),
        reader=FileReader(),
        renderer=MarkdownRenderer(),
    )

    with out_path.open("w", encoding="utf-8", newline="\n") as out:
        report = scanner.run(out)

    report.print_to(sys.stderr)

    if report.total == 0:
        print(
            "[scan] no input paths; did you forget to pipe git ls-files?",
            file=sys.stderr,
        )
        return 1
    return 0
