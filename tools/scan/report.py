"""扫描报告。"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from typing import TextIO


@dataclass
class Report:
    total: int = 0
    written: int = 0
    ignored: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    unreadable: list[str] = field(default_factory=list)

    def print_to(self, out: TextIO = sys.stderr) -> None:
        print(f"[scan] wrote {self.written} file(s)", file=out)
        if self.ignored:
            print(
                f"[scan] ignored {len(self.ignored)} path(s) by .scanignore:", file=out
            )
            for s in self.ignored:
                print(f"  - {s}", file=out)
        if self.missing:
            print(f"[scan] missing {len(self.missing)} path(s):", file=out)
            for s in self.missing:
                print(f"  - {s}", file=out)
        if self.unreadable:
            print(f"[scan] unreadable {len(self.unreadable)} path(s):", file=out)
            for s in self.unreadable:
                print(f"  - {s}", file=out)
