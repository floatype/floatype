"""Measures whether one correction of how a word is written spreads to other
phrases (issue #10): よい for 良い, ほう for 方, and 機会 for 機械 as a pair that is
not about writing.

Each test phrase runs on a line of its own, so that nothing a phrase teaches
reaches the next (replay_main clears what was learned at each line's end):
first alone, then after the correction. The correction is what a user does:
for よい, the pause, Right to the 良い segment and Space to よい; for the others,
the pause and Space, which shows Mozc's result again instead of zenz's.

Run from src/ after building //floatype/eval:replay_main and
//data_manager/oss:mozc.data:

  python3 floatype/eval/notation_learning.py [--context] [--user_files_dir D]

--context passes the text committed before as the text before the caret, as an
editor does; without it nothing is passed, as in a terminal. --between adds two
unrelated sentences after the correction.
"""

import argparse
import subprocess

PAUSE = "\v"
ENTER = "\r"
RIGHT = "\x10"

# (name, correction keys, phrases to test).
PAIRS = [
    ("よい／良い", "hasirutoyoi" + PAUSE + RIGHT + " " + ENTER,
     ["kakuninsurutoyoi", "idousurutoyoi", "syuuseisurutoyoi",
      "zokkousiteyoi", "kurabetahougayoi", "kaitemoyoi", "sorehayoikotoda"]),
    ("ほう／方", "kurabetahougayoi" + PAUSE + " " + ENTER,
     ["yattahougayoi", "minaihougaii", "hayaihougaraku", "konohoudesu",
      "anohouniikimasu"]),
    ("機会／機械（表記ではない組）", "kikaiwotukau" + PAUSE + " " + ENTER,
     ["kikaiwomatu", "koujounokikaigaugoku", "kikaiwonaosu"]),
]

UNRELATED = ("kinounoyoruhaamegafuttanodekasawomottekaerimasita" + PAUSE +
             ENTER + "asitanokaigihajuujikarakaigisitsudeokonaimasu" + PAUSE +
             ENTER)


def committed(binary, lines, flags):
  """What the last Enter of each line committed."""
  out = subprocess.run(
      [binary, "--live", "--keys", "--trace_keys", *flags],
      input="".join(line + "\n" for line in lines),
      capture_output=True, encoding="utf-8").stderr
  # What each Enter committed, in order; a line's last Enter is its phrase.
  enters = [row.split("\t")[3] for row in out.split("\n")
            if row.startswith("TRACE\tEnter") and len(row.split("\t")) > 3]
  results, seen = [], 0
  for line in lines:
    seen += line.count(ENTER)
    results.append(enters[seen - 1] if 0 < seen <= len(enters) else "")
  return results


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--binary", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--context", action="store_true")
  parser.add_argument("--between", action="store_true")
  parser.add_argument("--user_files_dir", default="")
  parser.add_argument("--zenz", default="true")
  parser.add_argument("--learn_chosen_words_everywhere", action="store_true")
  args = parser.parse_args()
  flags = [f"--zenz={args.zenz}"]
  if args.learn_chosen_words_everywhere:
    flags.append("--learn_chosen_words_everywhere")
  if not args.context:
    flags += ["--unknown_context", "--zenz_context_from_commits=false"]
  if args.user_files_dir:
    flags.append(f"--user_files_dir={args.user_files_dir}")
  for name, correction, phrases in PAIRS:
    before = committed(args.binary, [p + PAUSE + ENTER for p in phrases], flags)
    middle = UNRELATED if args.between else ""
    after = committed(args.binary,
                      [correction + middle + p + PAUSE + ENTER
                       for p in phrases], flags)
    print(f"## {name}")
    print("| 打ったもの | 直す前 | 1 回直したあと |")
    print("|---|---|---|")
    for phrase, b, a in zip(phrases, before, after):
      mark = "" if a == b else " ←"
      print(f"| {phrase} | {b} | {a}{mark} |")
    print()


if __name__ == "__main__":
  main()
