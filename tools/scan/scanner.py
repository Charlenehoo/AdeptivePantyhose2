"""调度：把所有部件串起来。"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, TextIO

from .file_reader import FileReader
from .ignore_rules import IgnoreRules
from .lang_map import LanguageMap
from .renderer import MarkdownRenderer
from .report import Report


@dataclass
class Scanner:
    """跑一次扫描并返回 Report。

    依赖全部由构造函数注入，方便测试时替换。
    """

    source: Iterable[str]
    ignore: IgnoreRules
    languages: LanguageMap
    reader: FileReader
    renderer: MarkdownRenderer

    def run(self, out: TextIO) -> Report:
        report = Report()

        for display in self.source:
            report.total += 1

            if self.ignore.matches(display):
                report.ignored.append(display)
                continue

            content, status = self.reader.read(display)
            if status == FileReader.MISSING:
                report.missing.append(display)
                continue
            if status == FileReader.UNREADABLE:
                report.unreadable.append(display)
                continue

            lang = self.languages.detect(display)
            out.write(self.renderer.render(display, lang, content))
            report.written += 1

        return report
