#!/usr/bin/env python3
"""Writes the extra dictionaries made from outside data (spec section 9) into
data/dictionary_oss/dictionary_floatype_ut_*.txt. They are not committed: the
data stays with its owners, and the build works without them. Each source is
pinned to a revision and a SHA-256 and downloaded once into
~/Library/Caches/floatype/dictionary.

Run from src/: python3 floatype/tools/make_extra_dictionary.py
"""

import bz2
import hashlib
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

# Parts of speech, by name in id.def.
NAME = "名詞,固有名詞,人名,一般,*,*,*"
SURNAME = "名詞,固有名詞,人名,姓,*,*,*"
GIVEN = "名詞,固有名詞,人名,名,*,*,*"
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


def self_check() -> None:
  assert keeps_name("かとうまき", "加藤麻希")
  assert not keeps_name("やっちゃん", "やっちゃん"), "no kanji"
  assert not keeps_name("えー", "Ａ子"), "letters"
  assert not keeps_name("か", "佳"), "one character"
  surnames, given = {("かとう", "加藤")}, {("まき", "麻希")}
  assert composable("かとうまき", "加藤麻希", surnames, given)
  assert not composable("かとうまき", "加藤真紀", surnames, given)


def main() -> None:
  self_check()
  ids = pos_ids()
  pairs, surnames, given = base_entries(ids)
  rows = personal_names(ids, pairs, surnames, given)
  out = OUT_DIR / "dictionary_floatype_ut_names.txt"
  out.write_text("".join(f"{row}\n" for row in rows))
  print(f"{out}: {len(rows)} entries", file=sys.stderr)


if __name__ == "__main__":
  main()
