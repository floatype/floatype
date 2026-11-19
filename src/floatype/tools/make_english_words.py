#!/usr/bin/env python3
"""Writes floatype/english/english_words.txt, the words mixed English input
keeps in letters: SCOWL's (ESDB's) size-60 American list, the size spell
checkers ship, lower-cased, letters only and three letters or more, as
LooksEnglish needs. Its notice is floatype/english/SCOWL-Copyright.

Run from src/ with a checkout of https://github.com/en-wl/wordlist at
SCOWL_COMMIT (it builds its database with make, about 45 seconds):
  python3 floatype/tools/make_english_words.py /path/to/wordlist
"""

import re
import subprocess
import sys
from pathlib import Path

SCOWL_COMMIT = "1e5b7d3a72f47a71da5d28686c1dd4b397178485"
OUT = Path("floatype/english/english_words.txt")


def words(listing: str) -> list[str]:
  return sorted({w.lower() for w in listing.split("\n")
                 if re.fullmatch(r"[A-Za-z]{3,}", w)})


def self_check() -> None:
  assert words("Email\nemail\nGitHub\nab\nco-op\ncafé\n") == ["email", "github"]


def main() -> None:
  self_check()
  scowl = Path(sys.argv[1])
  commit = subprocess.run(["git", "-C", scowl, "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout
  if commit.strip() != SCOWL_COMMIT:
    raise SystemExit(f"expected SCOWL at {SCOWL_COMMIT}, got {commit.strip()}")
  subprocess.run(["make", "-C", scowl, "scowl.db"], check=True,
                 capture_output=True)
  listing = subprocess.run(
      ["./scowl", "--db", "scowl.db", "word-list", "60", "A", "1"], cwd=scowl,
      capture_output=True, text=True, check=True).stdout
  OUT.write_text("".join(f"{w}\n" for w in words(listing)))
  (OUT.parent / "SCOWL-Copyright").write_text((scowl / "Copyright").read_text())


if __name__ == "__main__":
  main()
