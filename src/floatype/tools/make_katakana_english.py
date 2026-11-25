#!/usr/bin/env python3
"""Writes data/dictionary_oss/dictionary_floatype_english.txt: loanwords'
English source words (いんごっと → ingot), from UniDic's lemmas, which carry
them (インゴット-ingot). Mozc's English variants add Ingot and INGOT. Only
loanwords (語種 外) whose word is in floatype/english/english_words.txt, and
not one Mozc's dictionary has under any reading: the suggestion filter keeps
these words out of suggestions by the word, and would take Mozc's own too. UniDic is offered under the BSD
license among others; its notice goes to data/dictionary_oss/UniDic-BSD.

Run from src/ with UniDic for contemporary written Japanese 3.1.0 unzipped
(https://clrd.ninjal.ac.jp/unidic/):
  python3 floatype/tools/make_katakana_english.py /path/to/unidic-cwj-3.1.0
"""

import csv
import hashlib
import re
import sys
from collections import defaultdict
from pathlib import Path

LEX_SHA256 = "5c892d88de9bb87dd90a2c1b2492b4cce0d13dacd9888d25e05298a00612a376"
OUT = Path("data/dictionary_oss/dictionary_floatype_english.txt")
# They convert but are not suggested, as the other added words.
FILTER = Path("data/dictionary_oss/suggestion_filter_floatype_english.txt")
NOTICE = Path("data/dictionary_oss/UniDic-BSD")
ENGLISH = Path("floatype/english/english_words.txt")
MOZC = sorted(Path("data/dictionary_oss").glob("dictionary0*.txt"))
NOUN = 1851  # 名詞,一般 (id.def)
# Just below the katakana: Mozc's katakana word for the reading, with its
# part of speech, as a fixed cost lost to some (アクチブ costs 9100, and
# アグリカルチャー at 6562 lost to 9000 with another part of speech). Where
# Mozc has none, its unknown word costs more the longer it is (アクシデンタル
# lost to accidental at 12000) and a long reading often breaks into pieces
# (赤雲端と), so the katakana is added too.
BELOW = 10
KATAKANA_COST = 12000
PER_READING = 2  # クレーム: claim and cream, each with Ingot-like variants.

LEMMA = re.compile(r"(.+?)-([A-Za-z]+)")
KATAKANA = re.compile(r"[ァ-ヴー]{2,}")


def hiragana(katakana: str) -> str:
  return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヴ" else c
                 for c in katakana)


def katakana_of(reading: str) -> str:
  return "".join(chr(ord(c) + 0x60) if "ぁ" <= c <= "ゔ" else c
                 for c in reading)


def entries(rows, english: set[str], mozc_words: set[str],
            katakana_words: dict[str, tuple[int, int, int]]):
  """(reading, left id, right id, cost, word): at most PER_READING English
  words a reading, the most common (UniDic's lowest cost) first, after the
  katakana where Mozc lacks it. `mozc_words` holds Mozc's values,
  lower-cased; `katakana_words` its katakana word for a reading."""
  best = defaultdict(dict)  # reading -> word -> cost
  for row in rows:
    if len(row) < 17 or row[16] != "外":
      continue
    m = LEMMA.fullmatch(row[11])
    if (not m or m.group(2).lower() not in english
        or m.group(2).lower() in mozc_words):
      continue
    word, cost = m.group(2), int(row[3])
    for kana in {row[0], row[10]}:
      if not KATAKANA.fullmatch(kana):
        continue
      reading = hiragana(kana)
      best[reading][word] = min(cost, best[reading].get(word, cost))
  for reading in sorted(best):
    words = sorted(best[reading], key=lambda w: (best[reading][w], w))
    left, right, cost = katakana_words.get(reading, (NOUN, NOUN, KATAKANA_COST))
    if reading not in katakana_words:
      yield reading, NOUN, NOUN, KATAKANA_COST, katakana_of(reading)
    for i, word in enumerate(words[:PER_READING]):
      # A tie leaves Mozc's order to chance: veal came before beer.
      yield reading, left, right, cost + BELOW + i, word


def self_check() -> None:
  assert hiragana("インゴット") == "いんごっと"
  assert hiragana("ヴァイオリン") == "ゔぁいおりん"
  row = lambda surface, lemma, goshu, cost: (
      [surface, "0", "0", str(cost)] + ["*"] * 6 + [surface, lemma]
      + ["*"] * 4 + [goshu])
  got = list(entries(
      [row("インゴット", "インゴット-ingot", "外", 5000),
       row("クレーム", "クレーム-claim", "外", 4000),
       row("クレーム", "クレーム-cream", "外", 6000),
       row("クレーム", "クレーム-crème", "外", 3000),  # not letters only
       row("サクラ", "桜", "和", 3000),
       row("ゼミ", "ゼミ-Seminar", "外", 3000),
       row("テーマ", "テーマ-Thema", "外", 3000),  # German, not in the list
       row("データ", "データ-data", "外", 3000),  # Mozc has it
       row("A", "エー-A", "外", 3000)],
      {"ingot", "claim", "cream", "seminar", "data"},
      {"data"}, {"くれーむ": (1841, 1841, 4909)}))
  assert got == [
      ("いんごっと", NOUN, NOUN, KATAKANA_COST, "インゴット"),
      ("いんごっと", NOUN, NOUN, KATAKANA_COST + BELOW, "ingot"),
      ("くれーむ", 1841, 1841, 4909 + BELOW, "claim"),
      ("くれーむ", 1841, 1841, 4909 + BELOW + 1, "cream"),
      ("ぜみ", NOUN, NOUN, KATAKANA_COST, "ゼミ"),
      ("ぜみ", NOUN, NOUN, KATAKANA_COST + BELOW, "Seminar")], got


def main() -> None:
  self_check()
  unidic = Path(sys.argv[1])
  lex = unidic / "lex_3_1.csv"
  digest = hashlib.sha256(lex.read_bytes()).hexdigest()
  if digest != LEX_SHA256:
    raise SystemExit(f"expected lex_3_1.csv {LEX_SHA256}, got {digest}")
  english = set(ENGLISH.read_text().split())
  mozc_words, katakana_words = set(), {}
  for path in MOZC:
    for line in path.read_text(encoding="utf-8").splitlines():
      fields = line.split("\t")
      if len(fields) != 5:
        continue
      reading, left, right, cost, value = fields
      mozc_words.add(value.lower())
      if value != katakana_of(reading):
        continue
      # A plain noun's first: as a proper noun (インソムニア), Mozc's English
      # variants put INSOMNIA first in インソムニアを使う.
      key = (int(left) != NOUN, int(cost))
      old = katakana_words.get(reading)
      if old is None or key < (old[0] != NOUN, old[2]):
        katakana_words[reading] = (int(left), int(right), int(cost))
  with lex.open(encoding="utf-8", newline="") as f:
    rows = list(entries(csv.reader(f), english, mozc_words, katakana_words))
  OUT.write_text("".join("\t".join(map(str, row)) + "\n" for row in rows),
                 encoding="utf-8")
  # A katakana word Mozc has under another reading (エヴァー) stays suggested.
  FILTER.write_text("".join(f"{w}\n" for w in sorted(
      {row[4] for row in rows if row[4].lower() not in mozc_words})),
                    encoding="utf-8")
  NOTICE.write_text((unidic / "liceses" / "BSD").read_text(encoding="utf-8"),
                    encoding="utf-8")
  print(f"{len(rows)} entries")


if __name__ == "__main__":
  main()
