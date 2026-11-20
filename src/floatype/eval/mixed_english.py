"""Scores English words typed in a kana mode (mixed Japanese/English input).

The sets (--set) are mixed_english_dev.tsv, which the feature is tuned on, and
mixed_english_heldout.tsv, which it is judged on: for held-out items only the
counts are printed, never the items (--out still writes them all). Each line
is "<category>\t<left context>\t<keys>\t<expected>"; see the files' headers.

The keys of an item are typed one by one after its left context with live
conversion and mixed_english on, pausing once after the last key, and
committed with Enter (replay_main --live --mixed_english). The committed text
must be one of the expected answers, as it is or after NFKC. Where the answer
is all Japanese, an output with ASCII letters also counts as a false positive:
English seen where there was none.

Run from src/ after building the scorer and replay_main (see ajimee.py):
  python3 floatype/eval/mixed_english.py [--set dev|heldout|all] [--out /tmp/mixed.tsv]
"""

import argparse
import re
import subprocess
import sys
import unicodedata
from pathlib import Path

SETS = {"dev": ["dev"], "heldout": ["heldout"], "all": ["dev", "heldout"]}
CATEGORIES = ["english-alone", "english-loose", "japanese-alone", "mid-sentence",
              "ambiguous-japanese", "ambiguous-english", "slash",
              "japanese-sentence"]
# Their answers hold no English.
JAPANESE = ["japanese-alone", "ambiguous-japanese", "japanese-sentence"]
# What replay_main types as it is: no space, no upper case.
KEYS = re.compile(r"[!-@\[-~]+")
# zenz answers within 2 or 3 polls of 10 ms; its 300 ms timeout takes about 30.
TIMEOUT_POLLS = 20


def load_items(text, name):
  """A set's text -> [{"set", "category", "context", "keys", "expected": [...]}]."""
  items = []
  for number, line in enumerate(text.splitlines(), 1):
    if not line or line.startswith("#"):
      continue
    fields = line.split("\t")
    if (len(fields) != 4 or fields[0] not in CATEGORIES
        or not KEYS.fullmatch(fields[2]) or not fields[3]):
      raise SystemExit(f"{name} line {number}: not an item: {line!r}")
    items.append({"set": name, "category": fields[0], "context": fields[1],
                  "keys": fields[2], "expected": fields[3].split(" | ")})
  return items


def read_set(name):
  path = Path(__file__).resolve().parent / f"mixed_english_{name}.tsv"
  return load_items(path.read_text(encoding="utf-8"), name)


def nfkc(text):
  return unicodedata.normalize("NFKC", text)


def score(item, line):
  """An item and replay_main's line for it -> the item with its result."""
  fields = line.split("\t")
  skipped = line == "SKIP"
  got = "" if skipped else fields[0]
  return {
      **item, "got": got, "skipped": skipped,
      "exact": got in item["expected"],
      "nfkc": nfkc(got) in {nfkc(e) for e in item["expected"]},
      "false_positive": (item["category"] in JAPANESE
                         and re.search("[A-Za-z]", nfkc(got)) is not None),
      "timeout": not skipped and int(fields[2]) >= TIMEOUT_POLLS,
  }


def replay(items, args):
  done = subprocess.run(
      [args.replay, "--live", "--mixed_english", f"--scorer={args.scorer}"]
      + ([f"--model={args.model}"] if args.model else []),
      input="".join(f"{i['context']}\t{i['keys']}\n" for i in items),
      capture_output=True, encoding="utf-8", errors="replace", check=True)
  lines = done.stdout.splitlines()
  if len(lines) != len(items):
    raise SystemExit(f"expected {len(items)} results, got {len(lines)}")
  return [score(item, line) for item, line in zip(items, lines)]


def report(name, rows):
  def count(key, subset=rows):
    return sum(r[key] for r in subset)

  print(f"{name} {len(rows)} 件: 一致 {count('exact')}、NFKC で一致 {count('nfkc')}")
  for category in CATEGORIES:
    subset = [r for r in rows if r["category"] == category]
    print(f"  {category:<19}{len(subset):>4} 件: 一致 {count('exact', subset)}、"
          f"NFKC で一致 {count('nfkc', subset)}")
  japanese = [r for r in rows if r["category"] in JAPANESE]
  print(f"  日本語のはずの出力に英字: {count('false_positive')}/{len(japanese)} 件")
  print(f"  打てなかった（SKIP）: {count('skipped')} 件、"
        f"zenz の時間切れらしい: {count('timeout')} 件")
  if name == "heldout":
    return
  for r in rows:
    if not r["nfkc"]:
      mark = "（英字）" if r["false_positive"] else ""
      context = f"{r['context']}|" if r["context"] else ""
      print(f"  外れ {r['category']}: {context}{r['keys']} → {r['got']}{mark}"
            f"  正解 {' | '.join(r['expected'])}")


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--set", choices=list(SETS), default="dev")
  parser.add_argument("--replay", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--scorer", default="floatype/scorer/build/floatype_scorer")
  parser.add_argument("--model", help="the zenz model (default: replay_main's)")
  parser.add_argument(
      "--out", type=Path,
      help="write every item: set, category, left context, keys, expected, "
      "committed, then 1 or 0 for exact, NFKC, false positive, zenz timeout")
  args = parser.parse_args()

  rows = []
  for name in SETS[args.set]:
    rows += replay(read_set(name), args)
    report(name, [r for r in rows if r["set"] == name])
  if args.out:
    args.out.write_text("".join(
        "\t".join([r["set"], r["category"], r["context"], r["keys"],
                   " | ".join(r["expected"]), r["got"], str(int(r["exact"])),
                   str(int(r["nfkc"])), str(int(r["false_positive"])),
                   str(int(r["timeout"]))]) + "\n"
        for r in rows), encoding="utf-8")
  return 0


if __name__ == "__main__":
  sys.exit(main())
