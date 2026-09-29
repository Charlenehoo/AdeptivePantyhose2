"""路径 → markdown 代码块语言标签。"""

from __future__ import annotations

import json
import sys
from pathlib import Path


class LanguageMap:
    """把文件路径映射到 markdown 代码块的语言标签。"""

    def __init__(self, by_ext: dict[str, str], by_name: dict[str, str]) -> None:
        self._by_ext = by_ext
        self._by_name = by_name

    @classmethod
    def from_json(cls, path: Path) -> "LanguageMap":
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            print(f"[scan] config not found: {path}", file=sys.stderr)
            return cls({}, {})
        except json.JSONDecodeError as e:
            print(f"[scan] invalid JSON in {path}: {e}", file=sys.stderr)
            return cls({}, {})

        by_ext = {k.lower(): v for k, v in data.get("by_ext", {}).items()}
        by_name = {k.lower(): v for k, v in data.get("by_name", {}).items()}
        return cls(by_ext, by_name)

    def detect(self, path: str) -> str:
        name = path.rsplit("/", 1)[-1].lower()
        if name in self._by_name:
            return self._by_name[name]
        ext = Path(name).suffix.lower()
        return self._by_ext.get(ext, "")
