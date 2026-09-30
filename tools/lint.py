"""Clang-tidy wrapper. Reads file list from args or stdin."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

CPP_EXTS = {".cpp", ".cxx", ".cc", ".c++", ".h", ".hpp", ".hxx", ".h++"}

CLANG_TIDY_ARGS = [
    "--extra-arg=-fms-extensions",
    "--extra-arg=-fms-compatibility",
    "--extra-arg=-Wno-unknown-pragmas",
    "--extra-arg=-Wno-unknown-attributes",
    "--extra-arg=-Wno-ignored-attributes",
    "--extra-arg=-Wno-unused-command-line-argument",
    "--extra-arg=-D_ITERATOR_DEBUG_LEVEL=0",
]


def read_stdin() -> list[str]:
    if sys.stdin.isatty():
        return []
    return [line.strip() for line in sys.stdin if line.strip()]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run clang-tidy on given files.")
    parser.add_argument("files", nargs="*", help="files; if empty, read from stdin")
    parser.add_argument("-p", "--compile-commands", default="build/clang-tidy")
    parser.add_argument("--fix", action="store_true")
    parser.add_argument(
        "--header-filter", default=r".*AdeptivePantyhose2[\\/]src[\\/].*"
    )
    args = parser.parse_args()

    raw = args.files or read_stdin()

    root = Path.cwd()
    cpp_files = [
        str((root / f).resolve()) for f in raw if Path(f).suffix.lower() in CPP_EXTS
    ]

    if not cpp_files:
        print("no C++ files to lint", file=sys.stderr)
        return 0

    cmd = [
        "clang-tidy",
        "-p",
        args.compile_commands,
        f"--header-filter={args.header_filter}",
        *CLANG_TIDY_ARGS,
    ]
    if args.fix:
        cmd.append("--fix")
    cmd.extend(cpp_files)

    print(f"linting {len(cpp_files)} file(s)...", file=sys.stderr)
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
