#!/usr/bin/env python3
"""Compares two mozc.data files, before and after adding dictionary words
(floatype/tools/make_extra_dictionary.py), on what the regression set and
AJIMEE-Bench do not see. Run from src/ after building replay_main:
  python3 floatype/eval/dictionary_check.py BEFORE.data AFTER.data

- colloquial: floatype/eval/colloquial.tsv (reading, expected), right of 30;
- displaced: readings Mozc's dictionary already has that the added
  dictionaries also have, a fixed sample converted alone; how many change
  their first answer (no answer key: read the samples);
- suggestions: AJIMEE-Bench's readings cut to 2 to 4 kana and typed key by
  key; how many suggestion lists change, and their first suggestion.

Strings are compared in Python: macOS's awk takes 愛あむ and アイアム as equal.
"""

import argparse
import json
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ajimee import AJIMEE_SHA256, AJIMEE_URL, AJIMEE_COMMIT, fetch, katakana_to_hiragana  # noqa: E402

DICTIONARY = Path("data/dictionary_oss")


def replay(binary: str, data: str, readings: list[str], *flags: str) -> list[str]:
  out = subprocess.run(
      [binary, "--zenz=false", f"--data_file={data}", "--data_type=oss", *flags],
      input="".join(f"\t{r}\n" for r in readings), capture_output=True,
      encoding="utf-8", check=True).stdout.splitlines()
  return [line.split("\t")[0] if not flags else line for line in out]


def shared_readings(count: int) -> list[str]:
  base = set()
  for path in DICTIONARY.glob("dictionary*.txt"):
    if not path.name.startswith("dictionary_floatype_ut_"):
      base.update(line.split("\t", 1)[0] for line in path.open(encoding="utf-8"))
  added = set()
  for path in DICTIONARY.glob("dictionary_floatype_ut_*.txt"):
    added.update(line.split("\t", 1)[0] for line in path.open(encoding="utf-8"))
  shared = sorted(added & base)
  return sorted(random.Random(1).sample(shared, min(count, len(shared))))


def prefixes() -> list[str]:
  items = json.loads(fetch(AJIMEE_URL, f"ajimee-v1-{AJIMEE_COMMIT[:12]}.json",
                           AJIMEE_SHA256).read_bytes())
  cut = set()
  for item in items:
    reading = katakana_to_hiragana(item["input"])
    for n in (2, 3, 4):
      head = reading[:n]
      if len(head) == n and re.fullmatch("[ぁ-ゖー]+", head) and not head.endswith("っ"):
        cut.add(head)
  return sorted(cut)


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("before")
  parser.add_argument("after")
  parser.add_argument("--replay", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--samples", type=int, default=3000)
  args = parser.parse_args()

  rows = [line.split("\t") for line in
          Path("floatype/eval/colloquial.tsv").read_text(encoding="utf-8").splitlines()]
  for name in ("before", "after"):
    out = replay(args.replay, getattr(args, name), [r[0] for r in rows])
    right = sum(r[1] == o for r, o in zip(rows, out))
    print(f"colloquial {name}: {right}/{len(rows)}")

  readings = shared_readings(args.samples)
  before = replay(args.replay, args.before, readings)
  after = replay(args.replay, args.after, readings)
  changed = [(r, b, a) for r, b, a in zip(readings, before, after) if b != a]
  print(f"displaced: {len(changed)} of {len(readings)} shared readings change")
  for r, b, a in changed[::max(1, len(changed) // 20)]:
    print(f"  {r}: {b} → {a}")

  heads = prefixes()
  before = replay(args.replay, args.before, heads, "--suggest")
  after = replay(args.replay, args.after, heads, "--suggest")
  same = sum(b == a for b, a in zip(before, after))
  first = sum(b.split("|")[0] == a.split("|")[0] for b, a in zip(before, after))
  print(f"suggestions: {same} of {len(heads)} lists unchanged, "
        f"{first} with the same first suggestion")


if __name__ == "__main__":
  main()
