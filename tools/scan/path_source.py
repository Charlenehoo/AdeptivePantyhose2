"""路径来源。"""

from __future__ import annotations

from typing import Iterator, TextIO


class StdinPathSource:
    """从文本流逐行读取路径列表，规范化分隔符为 /。"""

    def __init__(self, stream: TextIO) -> None:
        self._stream = stream

    def __iter__(self) -> Iterator[str]:
        for line in self._stream:
            line = line.strip()
            if line:
                yield line.replace("\\", "/")
