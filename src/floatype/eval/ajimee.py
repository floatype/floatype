"""Measure kana-kanji conversion accuracy on AJIMEE-Bench v1.

Mozc converts every reading with //converter:quality_regression_main; this script
compares the top-1 result with the accepted answers. Build the inputs first:

  bazelisk build --config release_build \
      //converter:quality_regression_main //data_manager/oss:mozc.data

Then run from src/:

  python3 floatype/eval/ajimee.py --out /tmp/ajimee-mozc.jsonl

Metrics: Acc@1 is the official exact match against any accepted answer. Acc@1
(NFKC) applies NFKC to both sides, which absorbs full-width/half-width
differences. MinCER is the official minimum character error rate.
"""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import unicodedata
import urllib.request
from pathlib import Path

AJIMEE_COMMIT = "401666cd56d1a570c2021798b64b6da4396bfd45"
AJIMEE_URL = (
    "https://raw.githubusercontent.com/azooKey/AJIMEE-Bench/"
    f"{AJIMEE_COMMIT}/JWTD_v2/v1/evaluation_items.json"
)
AJIMEE_SHA256 = "e9eb668fd6aa14b1e26436f429b5550108af0a1dfd443b8cea0bcb3ab3028fca"
CACHE_DIR = Path(__file__).resolve().parent / ".cache"


def katakana_to_hiragana(text: str) -> str:
  """Converts katakana (U+30A1..U+30F6) to hiragana and leaves the rest."""
  return "".join(
      chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text
  )


def levenshtein(a: str, b: str) -> int:
  previous = list(range(len(b) + 1))
  for i, ca in enumerate(a, 1):
    current = [i]
    for j, cb in enumerate(b, 1):
      current.append(
          min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (ca != cb))
      )
    previous = current
  return previous[-1]


def min_cer(references: list[str], hypothesis: str) -> float:
  return min(levenshtein(r, hypothesis) / len(r) for r in references)


def nfkc(text: str) -> str:
  return unicodedata.normalize("NFKC", text)


def score(item: dict, top1: str) -> dict:
  refs = item["expected_output"]
  return {
      "index": item["index"],
      "has_context": bool(item.get("context_text")),
      "reading": item["input"],
      "top1": top1,
      "expected": refs,
      "exact": top1 in refs,
      "nfkc": nfkc(top1) in {nfkc(r) for r in refs},
      "min_cer": min_cer(refs, top1),
  }


def summarize(rows: list[dict]) -> dict:
  def part(subset: list[dict]) -> dict:
    n = len(subset)
    return {
        "n": n,
        "acc1": sum(r["exact"] for r in subset) / n if n else 0.0,
        "acc1_nfkc": sum(r["nfkc"] for r in subset) / n if n else 0.0,
        "min_cer": sum(r["min_cer"] for r in subset) / n if n else 0.0,
    }

  return {
      "all": part(rows),
      "with_context": part([r for r in rows if r["has_context"]]),
      "without_context": part([r for r in rows if not r["has_context"]]),
  }


def load_items() -> list[dict]:
  path = CACHE_DIR / f"ajimee-v1-{AJIMEE_COMMIT[:12]}.json"
  if not path.exists():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(AJIMEE_URL, timeout=60) as response:
      path.write_bytes(response.read())
  data = path.read_bytes()
  digest = hashlib.sha256(data).hexdigest()
  if digest != AJIMEE_SHA256:
    raise SystemExit(f"unexpected AJIMEE-Bench data: sha256 {digest} at {path}")
  return json.loads(data)


def convert_with_mozc(readings: list[str], binary: Path, data_file: Path) -> list[str]:
  """Returns Mozc's top-1 conversion for each reading, in order."""
  with tempfile.TemporaryDirectory() as tmp:
    tests = Path(tmp) / "items.tsv"
    output = Path(tmp) / "out.tsv"
    # The expected value is irrelevant here; only the converted value is used.
    tests.write_text(
        "".join(f"ajimee\t{r}\t-\tConversion Expected\n" for r in readings),
        encoding="utf-8",
    )
    subprocess.run(
        [
            str(binary),
            f"--test_files={tests}",
            f"--data_file={data_file}",
            "--data_type=oss",
            f"--output={output}",
        ],
        check=True,
    )
    lines = output.read_text(encoding="utf-8").splitlines()
  # Each line: "OK:|FAILED:" \t key \t actual \t command \t expected \t
  results = [line.split("\t")[2] for line in lines]
  if len(results) != len(readings):
    raise SystemExit(f"expected {len(readings)} results, got {len(results)}")
  return results


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument(
      "--binary", type=Path, default=Path("bazel-bin/converter/quality_regression_main")
  )
  parser.add_argument(
      "--data_file", type=Path, default=Path("bazel-bin/data_manager/oss/mozc.data")
  )
  parser.add_argument("--out", type=Path, help="write per-item results as JSON lines")
  args = parser.parse_args()

  items = load_items()
  readings = [katakana_to_hiragana(item["input"]) for item in items]
  top1 = convert_with_mozc(readings, args.binary, args.data_file)
  rows = [score(item, t) for item, t in zip(items, top1)]
  if args.out:
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows),
        encoding="utf-8",
    )
  json.dump(summarize(rows), sys.stdout, indent=2)
  print()


if __name__ == "__main__":
  main()
