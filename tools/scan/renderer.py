"""渲染。"""

from __future__ import annotations


class MarkdownRenderer:
    """把 (路径, 语言, 内容) 渲染成一个 markdown 片段。"""

    def render(self, path: str, lang: str, content: str) -> str:
        body = content.rstrip("\n")
        return f"# {path}\n```{lang}\n{body}\n```\n\n"
