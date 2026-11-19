#!/usr/bin/env python3
"""Writes the extra dictionaries made from outside data (spec section 9) into
data/dictionary_oss/dictionary_floatype_ut_*.txt. They are not committed: the
data stays with its owners, and the build works without them. Each source is
pinned to a revision and a SHA-256 and downloaded once into
~/Library/Caches/floatype/dictionary.

Run from src/: python3 floatype/tools/make_extra_dictionary.py
"""

import bisect
import bz2
import hashlib
import html
import re
import sys
import urllib.request
from pathlib import Path

CACHE = Path.home() / "Library/Caches/floatype/dictionary"
OUT_DIR = Path("data/dictionary_oss")

# utuhiro78/mozcdic-ut-personal-names (Apache License 2.0).
PERSONAL_NAMES = (
    "https://raw.githubusercontent.com/utuhiro78/mozcdic-ut-personal-names/"
    "b557dd4c1548cc7c2164d5f8b14170c40cbb6f81/mozcdic-ut-personal-names.txt.bz2",
    "610e2e52fad15df0edac854960a05451a7e9a62f3df09c3f065db9e1ab6f51f4",
)

# utuhiro78/mozcdic-ut-place-names, from Japan Post's ZIP code data (no
# copyright claimed).
PLACE_NAMES = (
    "https://raw.githubusercontent.com/utuhiro78/mozcdic-ut-place-names/"
    "98cf8e9d7731695a52e5bc72ed633933c71d1913/mozcdic-ut-place-names.txt.bz2",
    "71a3e28e8acead3793739ec3466826b01adedc168354957661ec57c18688a0a2",
)

# utuhiro78/mozcdic-ut-sudachidict, from WorksApplications' SudachiDict
# (Apache License 2.0, with UniDic's and NEologd's notices).
SUDACHIDICT = (
    "https://raw.githubusercontent.com/utuhiro78/mozcdic-ut-sudachidict/"
    "13129ddebe1cb8eabb63feccbe157adec4e1de01/mozcdic-ut-sudachidict.txt.bz2",
    "1b9555338cc36606a5f497a2f57a0f9b948fa02d8c13d18ee9cfb5c99fb9d86e",
)

# Niccori250k/dic-nico-intersection-pixiv: words in both the Nico Nico and
# the pixiv encyclopedias (code MIT; no copyright claimed on the list).
NICO_PIXIV = (
    "https://raw.githubusercontent.com/Niccori250k/dic-nico-intersection-pixiv/"
    "4d0d8d219d0b9abf34745016e398e762182fce61/public/"
    "dic-nico-intersection-pixiv-google.txt",
    "e16990b24d75fccac9cf2b0b4382e59988e735bc9fa634626078dc500e0b8bc4",
)

# Japanese Wikipedia's article titles (CC BY-SA), only to tell how well
# known a word is; no title becomes an entry.
JAWIKI_TITLES = (
    "https://dumps.wikimedia.org/jawiki/20261001/"
    "jawiki-20261001-pages-articles-multistream-index.txt.bz2",
    "8a040564302dc0ba57160172abef3d4f69cc7ca36ea67b6439959d441cd037e7",
)

# Parts of speech, by name in id.def.
NAME = "名詞,固有名詞,人名,一般,*,*,*"
SURNAME = "名詞,固有名詞,人名,姓,*,*,*"
GIVEN = "名詞,固有名詞,人名,名,*,*,*"
PLACE = "名詞,固有名詞,地域,一般,*,*,*"
NOUN = "名詞,一般,*,*,*,*,*"
PROPER = "名詞,固有名詞,一般,*,*,*,*"
# Net words and titles rank after dictionary words of the same fame.
NICO_EXTRA_COST = 300
# Above most of Mozc's names (5000 to 8000), so a name never pushes an
# ordinary word aside; it shows when nothing else spells the reading.
NAME_COST = 9000

KANA = r"ぁ-ゖァ-ヺー"
KANJI = r"々一-鿿"


def fetch(url: str, sha256: str) -> bytes:
  path = CACHE / url.rsplit("/", 1)[1]
  if not path.exists():
    CACHE.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=120) as response:
      path.with_suffix(".partial").write_bytes(response.read())
    path.with_suffix(".partial").rename(path)
  data = path.read_bytes()
  if hashlib.sha256(data).hexdigest() != sha256:
    raise SystemExit(f"{path} does not match its SHA-256; delete it to fetch again")
  return data


def pos_ids() -> dict[str, int]:
  ids = {}
  for line in (OUT_DIR / "id.def").read_text().splitlines():
    number, name = line.split(" ", 1)
    ids[name] = int(number)
  return ids


def base_entries(ids: dict[str, int]):
  """(reading, value) pairs Mozc has, and its surnames and given names."""
  pairs, surnames, given = set(), set(), set()
  for path in sorted(OUT_DIR.glob("dictionary*.txt")):
    if path.name.startswith("dictionary_floatype_ut_"):
      continue
    for line in path.read_text().splitlines():
      reading, left, _, _, value = line.split("\t")[:5]
      pairs.add((reading, value))
      if int(left) == ids[SURNAME]:
        surnames.add((reading, value))
      elif int(left) == ids[GIVEN]:
        given.add((reading, value))
  return pairs, surnames, given


def composable(reading: str, value: str, surnames, given) -> bool:
  """Whether Mozc can already write the name as a surname and a given name."""
  for i in range(1, len(reading)):
    for j in range(1, len(value)):
      if ((reading[:i], value[:j]) in surnames
          and (reading[i:], value[j:]) in given):
        return True
  return False


class Titles:
  """Counts Wikipedia titles that start with a word, the way merge-ut weighs
  words: a word many articles start with (高尾山, 高尾山口駅...) is well known."""

  def __init__(self, text: str):
    titles = set()
    for line in text.splitlines():
      title = html.unescape(line.split(":", 2)[2])
      if ":" in title:  # Wikipedia:, Category: and other namespaces.
        continue
      titles.add(title.split(" (")[0])
    self.titles = sorted(titles)

  def hits(self, word: str, cap: int = 30) -> int:
    start = bisect.bisect_left(self.titles, word)
    end = bisect.bisect_left(self.titles, word + "\uffff", start)
    return min(end - start, cap)


def known_cost(hits: int) -> int:
  """Unknown words rank last; the better known, the cheaper, never below
  most of Mozc's ordinary nouns."""
  return 9500 if hits == 0 else max(7500, 8800 - 50 * hits)


def keeps_name(reading: str, value: str) -> bool:
  return (re.fullmatch(f"[ぁ-ゖー]{{2,}}", reading) is not None
          and re.fullmatch(f"[{KANA}{KANJI}]{{2,}}", value) is not None
          and re.search(f"[{KANJI}]", value) is not None)


def personal_names(ids, pairs, surnames, given) -> list[str]:
  text = bz2.decompress(fetch(*PERSONAL_NAMES)).decode("utf-8")
  rows = set()
  for line in text.splitlines():
    reading, _, _, _, value = line.split("\t")
    if (keeps_name(reading, value) and (reading, value) not in pairs
        and not composable(reading, value, surnames, given)):
      rows.add(f"{reading}\t{ids[NAME]}\t{ids[NAME]}\t{NAME_COST}\t{value}")
  return sorted(rows)


def ut_rows(source, ids, pairs, seen, pos, keep) -> list[str]:
  """Rows of a merge-ut child dictionary Mozc and earlier sources lack."""
  rows = []
  text = bz2.decompress(fetch(*source)).decode("utf-8")
  for line in text.splitlines():
    reading, _, _, _, value = line.split("\t")
    if (reading, value) in pairs or (reading, value) in seen:
      continue
    cost = keep(reading, value)
    if cost is not None:
      seen.add((reading, value))
      rows.append(f"{reading}\t{ids[pos]}\t{ids[pos]}\t{cost}\t{value}")
  return sorted(rows)


def plain(reading: str, value: str) -> bool:
  return (re.fullmatch("[\u3041-\u3096\u30fc]+", reading) is not None
          and 2 <= len(value) <= 25)


def sudachi_cost(titles: Titles, surnames, given, reading: str, value: str):
  """Words of letters alone only when Wikipedia knows them, as merge-ut does.
  Names Mozc writes from a surname and a given name stay out: as plain nouns
  they beat phrases (はやまるな became 葉山瑠菜)."""
  if not plain(reading, value) or composable(reading, value, surnames, given):
    return None
  hits = titles.hits(value)
  if hits == 0 and value.isascii():
    return None
  return known_cost(hits)


def nico_pixiv(titles, ids, pairs, seen, surnames, given) -> list[str]:
  rows = []
  for line in fetch(*NICO_PIXIV).decode("utf-8").splitlines():
    if not line or line.startswith("#"):
      continue
    reading, value = line.split("\t")[:2]
    if ((reading, value) in pairs or (reading, value) in seen
        or value == reading or not plain(reading, value)
        # Titles that are sentences (…どこですか?) and symbols stay out.
        or not re.fullmatch(f"[{KANA}{KANJI}A-Za-z0-9・]+", value)
        or composable(reading, value, surnames, given)):
      continue
    hits = titles.hits(value)
    if hits == 0 and value.isascii():
      continue
    seen.add((reading, value))
    cost = known_cost(hits) + NICO_EXTRA_COST
    rows.append(f"{reading}\t{ids[PROPER]}\t{ids[PROPER]}\t{cost}\t{value}")
  return sorted(rows)


def self_check() -> None:
  assert keeps_name("かとうまき", "加藤麻希")
  assert not keeps_name("やっちゃん", "やっちゃん"), "no kanji"
  assert not keeps_name("えー", "Ａ子"), "letters"
  assert not keeps_name("か", "佳"), "one character"
  surnames, given = {("かとう", "加藤")}, {("まき", "麻希")}
  assert composable("かとうまき", "加藤麻希", surnames, given)
  assert not composable("かとうまき", "加藤真紀", surnames, given)
  titles = Titles("1:2:高尾山\n1:3:高尾山口駅\n1:4:高尾 (曖昧さ回避)\n1:5:Category:高尾\n")
  assert titles.hits("高尾山") == 2 and titles.hits("高尾") == 3
  assert titles.hits("低尾") == 0
  assert known_cost(0) == 9500 and known_cost(1) == 8750 and known_cost(30) == 7500


def main() -> None:
  self_check()
  ids = pos_ids()
  pairs, surnames, given = base_entries(ids)
  titles = Titles(bz2.decompress(fetch(*JAWIKI_TITLES)).decode("utf-8"))
  seen = set()
  outputs = {
      "names": personal_names(ids, pairs, surnames, given),
      "places": ut_rows(
          PLACE_NAMES, ids, pairs, seen, PLACE,
          lambda r, v: known_cost(titles.hits(v)) if plain(r, v) else None),
      "sudachi": ut_rows(SUDACHIDICT, ids, pairs, seen, NOUN,
                         lambda r, v: sudachi_cost(titles, surnames, given, r, v)),
      "nico_pixiv": nico_pixiv(titles, ids, pairs, seen, surnames, given),
  }
  for name, rows in outputs.items():
    out = OUT_DIR / f"dictionary_floatype_ut_{name}.txt"
    out.write_text("".join(f"{row}\n" for row in rows))
    print(f"{out}: {len(rows)} entries", file=sys.stderr)


if __name__ == "__main__":
  main()
