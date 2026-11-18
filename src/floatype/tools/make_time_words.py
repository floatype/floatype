#!/usr/bin/env python3
"""Writes data/dictionary_oss/dictionary_floatype_time.txt: days of the month,
hours and minutes Mozc's dictionary lacks (にじゅうよっか, じゅうにじ,
にじゅっぷん), made from the rules of Japanese counting, so they carry the
project's license. Only readings of four kana or more, which ordinary words
rarely share. Run from src/: python3 floatype/tools/make_time_words.py
"""

from pathlib import Path

# Parts of speech (id.def): a plain noun on the left, as Mozc's own 4日, and
# the counter on the right, so 30分 joins 後 as 三十分後 does (a plain noun
# there made 30分語). A number on the left lost 10時 to 従事 instead.
NOUN = 1851  # 名詞,一般
COUNTER = {"分": 2004,  # 名詞,接尾,副詞可能,*,*,*,分
           "間": 2011,  # 名詞,接尾,助数詞
           "時": 2007,  # 名詞,接尾,副詞可能,*,*,*,時
           "日": 2011,  # 名詞,接尾,助数詞
           "半": 2011}
# Below Mozc's 2日 (3848) to 4日 (5504); 3500 to 4500 scored the same on the
# probe set (floatype/eval/time_words.tsv).
COST = 4000
# The kanji form after the digits, as Mozc's 10日 (4243) and 十日 (6246).
KANJI_COST = 6000

ONES = ["", "いち", "に", "さん", "よん", "ご", "ろく", "なな", "はち", "きゅう"]


def tens(n: int) -> str:
  return {1: "じゅう", 2: "にじゅう", 3: "さんじゅう", 4: "よんじゅう",
          5: "ごじゅう"}[n]


def day_readings(d: int) -> list[str]:
  special = {14: ["じゅうよっか"], 20: ["はつか"], 24: ["にじゅうよっか"]}
  if d in special:
    return special[d]
  t, o = divmod(d, 10)
  head = tens(t)
  if o == 0:
    return [head + "にち"]
  ones = {1: ["いち"], 2: ["に"], 3: ["さん"], 5: ["ご"], 6: ["ろく"],
          7: ["しち", "なな"], 8: ["はち"], 9: ["く", "きゅう"]}[o]
  return [head + one + "にち" for one in ones]


def hour_readings(h: int) -> list[str]:
  if h < 10:
    return {1: ["いちじ"], 2: ["にじ"], 3: ["さんじ"], 4: ["よじ"], 5: ["ごじ"],
            6: ["ろくじ"], 7: ["しちじ", "ななじ"], 8: ["はちじ"],
            9: ["くじ"]}[h]
  t, o = divmod(h, 10)
  return [tens(t) + r[: -len("じ")] + "じ" if o else tens(t) + "じ"
          for r in (hour_readings(o) if o else [""])]


def minute_readings(m: int) -> list[str]:
  t, o = divmod(m, 10)
  if o == 0:
    head = {1: "", 2: "に", 3: "さん", 4: "よん", 5: "ご"}[t]
    return [head + "じゅっぷん", head + "じっぷん"]
  ones = {1: ["いっぷん"], 2: ["にふん"], 3: ["さんぷん"], 4: ["よんぷん"],
          5: ["ごふん"], 6: ["ろっぷん"], 7: ["ななふん"],
          8: ["はっぷん", "はちふん"], 9: ["きゅうふん"]}[o]
  return [tens(t) + one for one in ones]


KANJI = "〇一二三四五六七八九"


def kanji(n: int) -> str:
  """Kanji numerals as Mozc writes 十五 and 二十四."""
  t, o = divmod(n, 10)
  return ((KANJI[t] if t > 1 else "") + ("十" if t else "")
          + (KANJI[o] if o else ""))


def entries() -> list[tuple[str, str]]:
  rows = []
  for d in range(11, 32):
    rows += [(r, f"{d}日") for r in day_readings(d)]
  for h in range(10, 25):
    rows += [(r, f"{h}時") for r in hour_readings(h)]
  for h in range(1, 25):
    rows += [(r + "はん", f"{h}時半") for r in hour_readings(h)]
  for m in range(10, 60):
    rows += [(r, f"{m}分") for r in minute_readings(m)]
  # Durations, or 30日間 becomes 30日感 and 15分間 15分澗.
  rows += [(r + "かん", s + "間") for r, s in list(rows)
           if s[-1] in "日分" or (s[-1] == "時" and not s.endswith("時半"))]
  for h in range(1, 10):
    rows += [(r + "かん", f"{h}時間") for r in hour_readings(h)]
  # にじゅうじ is far more often に従事 than 20時.
  return [(r, s) for r, s in rows if len(r) >= 4 and r != "にじゅうじ"]


def self_check() -> None:
  got = dict(entries())
  for reading, surface in [("にじゅうよっか", "24日"), ("じゅうくにち", "19日"),
                           ("じゅうきゅうにち", "19日"), ("はつか", None),
                           ("じゅうにじ", "12時"), ("じゅうしちじ", "17時"),
                           ("じゅうななじ", "17時"), ("にじゅうよじ", "24時"),
                           ("じゅうじはん", "10時半"), ("くじはん", "9時半"),
                           ("にじゅっぷん", "20分"), ("じゅうごふん", "15分"),
                           ("ごじゅうきゅうふん", "59分"), ("さんじゅうはっぷん", "38分"),
                           ("さんじゅうにちかん", "30日間"), ("じゅうごふんかん", "15分間"),
                           ("にじゅうよじかん", "24時間"), ("いちじかん", "1時間"),
                           ("にじゅうじかん", "20時間"), ("にじゅうじ", None),
                           ("じゅうじはんかん", None)]:
    assert got.get(reading) == surface, (reading, got.get(reading))


def kanji_surface(surface: str) -> str:
  """24日 as 二十四日, for the kanji forms Mozc also lists (十日 after 10日)."""
  digits = "".join(c for c in surface if c.isdigit())
  return kanji(int(digits)) + surface[len(digits):]


def main() -> None:
  self_check()
  assert kanji_surface("24日") == "二十四日" and kanji_surface("10時半") == "十時半"
  assert kanji_surface("15分") == "十五分" and kanji_surface("30分") == "三十分"
  rows = []
  for r, s in entries():
    rows.append(f"{r}\t{NOUN}\t{COUNTER[s[-1]]}\t{COST}\t{s}\n")
    rows.append(f"{r}\t{NOUN}\t{COUNTER[s[-1]]}\t{KANJI_COST}\t"
                f"{kanji_surface(s)}\n")
  out = Path("data/dictionary_oss/dictionary_floatype_time.txt")
  out.write_text("".join(rows), encoding="utf-8")
  print(f"{out}: {len(rows)} entries")


if __name__ == "__main__":
  main()
