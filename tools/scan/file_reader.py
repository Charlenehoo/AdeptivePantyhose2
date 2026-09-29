"""文件读取。"""

from __future__ import annotations

import os


class FileReader:
    """把磁盘文件读成字符串，区分「不存在」和「读不了」。"""

    OK = "ok"
    MISSING = "missing"
    UNREADABLE = "unreadable"

    def __init__(self, encoding: str = "utf-8") -> None:
        self._encoding = encoding

    def read(self, path: str) -> tuple[str | None, str]:
        if not os.path.isfile(path):
            return None, self.MISSING
        try:
            with open(path, "r", encoding=self._encoding) as f:
                return f.read(), self.OK
        except (UnicodeDecodeError, OSError):
            return None, self.UNREADABLE
