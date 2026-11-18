#!/usr/bin/env python3
"""Scores Mozc's Space conversion on days, hours and minutes, and on words
that share their readings (floatype/eval/time_words.tsv: reading, expected).
Digits are compared in half width. Run from src/ after building replay_main:
  python3 floatype/eval/time_words.py [--data_file PATH]
"""

import argparse
import subprocess
from pathlib import Path


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--replay", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--data_file", default="bazel-bin/data_manager/oss/mozc.data")
  args = parser.parse_args()
  rows = [line.split("\t") for line in
          Path("floatype/eval/time_words.tsv").read_text(encoding="utf-8").splitlines()]
  out = subprocess.run(
      [args.replay, "--zenz=false", f"--data_file={args.data_file}"],
      input="".join(f"\t{reading}\n" for reading, _ in rows),
      capture_output=True, text=True, check=True).stdout.splitlines()
  half = str.maketrans("０１２３４５６７８９", "0123456789")
  misses = [(e, o.split("\t")[0].translate(half)) for (_, e), o in zip(rows, out)
            if o.split("\t")[0].translate(half) != e]
  print(f"{len(rows) - len(misses)}/{len(rows)} 正解")
  for expected, got in misses:
    print(f"  {expected} → {got}")


if __name__ == "__main__":
  main()
