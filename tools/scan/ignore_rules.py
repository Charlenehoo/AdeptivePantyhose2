"""按 .gitignore 子集语义判断路径是否该跳过。"""

from __future__ import annotations

import fnmatch
from pathlib import Path


class IgnoreRules:
    """按 .gitignore 子集语义判断路径是否该跳过。

    规则：
        - 空行、# 开头的行忽略
        - 模式不含 / 时，匹配路径中的任意一段
        - 模式含 / 时，匹配完整相对路径
        - 末尾 / 会被忽略
        - 支持 * 通配符
    """

    def __init__(self, patterns: list[str]) -> None:
        self._patterns = patterns

    @classmethod
    def from_file(cls, path: Path) -> "IgnoreRules":
        if not path.is_file():
            return cls([])

        patterns: list[str] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            line = line.rstrip("/")
            if line:
                patterns.append(line)
        return cls(patterns)

    def matches(self, display_path: str) -> bool:
        parts = display_path.split("/")
        for pat in self._patterns:
            if "/" in pat:
                if fnmatch.fnmatchcase(display_path, pat):
                    return True
            else:
                if any(fnmatch.fnmatchcase(p, pat) for p in parts):
                    return True
        return False
