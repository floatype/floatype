"""Measures what zenz does to Mozc's own regression set.

Takes the "Conversion Expected" rows of data/test/quality_regression_test
(oss.tsv and regression.tsv: a reading and the conversion Mozc must give),
converts each with Space through replay_main (the real engine and scorer, no
left context), and counts where zenz's correction wins (Mozc wrong, final
right) and where it harms (Mozc right, final wrong). The harms are what the
protection rules are for.

Run from src/ after building the scorer and replay_main (see ajimee.py):
  python3 floatype/eval/regression.py [--out /tmp/regression.tsv]
"""

import argparse
import subprocess
import sys
from pathlib import Path

FILES = ["data/test/quality_regression_test/oss.tsv",
         "data/test/quality_regression_test/regression.tsv"]


def load_rows(paths):
  rows = []
  for path in paths:
    for line in Path(path).read_text(encoding="utf-8").splitlines():
      fields = line.split("\t")
      if len(fields) >= 4 and fields[3] == "Conversion Expected":
        rows.append((fields[1], fields[2]))
  return rows


def tally(rows, results):
  """(reading, expected) rows and (mozc, final) results -> counts and lists."""
  counts = {"total": len(rows), "mozc": 0, "final": 0, "wins": [], "harms": []}
  for (reading, expected), (mozc, final) in zip(rows, results):
    counts["mozc"] += mozc == expected
    counts["final"] += final == expected
    if mozc != expected and final == expected:
      counts["wins"].append((reading, mozc, final))
    if mozc == expected and final != expected:
      counts["harms"].append((reading, mozc, final))
  return counts


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--replay", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--scorer", default="floatype/scorer/build/floatype_scorer")
  parser.add_argument("--out", type=Path, help="write every row's results")
  parser.add_argument("--model", help="the zenz model (default: replay_main's)")
  args = parser.parse_args()

  rows = load_rows(FILES)
  done = subprocess.run(
      [args.replay, f"--scorer={args.scorer}"]
      + ([f"--model={args.model}"] if args.model else []),
      input="".join(f"\t{reading}\n" for reading, _ in rows),
      capture_output=True, text=True, check=True)
  results = [tuple(line.split("\t")[:2]) for line in done.stdout.splitlines()]
  counts = tally(rows, results)
  print(f"{counts['total']} 件: Mozc {counts['mozc']} 正解、zenz の補正後 {counts['final']} 正解")
  print(f"勝ち {len(counts['wins'])}、害 {len(counts['harms'])}")
  for reading, mozc, final in counts["harms"]:
    print(f"  害: {reading}  Mozc {mozc} → {final}")
  if args.out:
    args.out.write_text("".join(
        f"{reading}\t{expected}\t{mozc}\t{final}\n"
        for (reading, expected), (mozc, final) in zip(rows, results)),
        encoding="utf-8")
  return 0


if __name__ == "__main__":
  sys.exit(main())
