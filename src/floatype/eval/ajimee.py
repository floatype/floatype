"""Measure kana-kanji conversion accuracy on AJIMEE-Bench v1.

Engines:
  mozc  converts every reading with //converter:quality_regression_main.
        Build first: bazelisk build --config release_build \
            //converter:quality_regression_main //data_manager/oss:mozc.data
  zenz  runs zenz-v3.2-small through floatype_scorer, the scorer process (greedy
        decoding), sending one request at a time.
        Build first: cmake -S floatype/scorer -B floatype/scorer/build \
            -DCMAKE_BUILD_TYPE=Release && cmake --build floatype/scorer/build

Run from src/, for example:

  python3 floatype/eval/ajimee.py --engine zenz --out /tmp/ajimee-zenz.jsonl

Metrics: Acc@1 is the official exact match against any accepted answer. Acc@1
(NFKC) applies NFKC to both sides, which absorbs full-width/half-width
differences. MinCER is the official minimum character error rate.
"""

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
import tempfile
import unicodedata
import urllib.request
from pathlib import Path

CACHE_DIR = Path(__file__).resolve().parent / ".cache"

AJIMEE_COMMIT = "401666cd56d1a570c2021798b64b6da4396bfd45"
AJIMEE_URL = (
    "https://raw.githubusercontent.com/azooKey/AJIMEE-Bench/"
    f"{AJIMEE_COMMIT}/JWTD_v2/v1/evaluation_items.json"
)
AJIMEE_SHA256 = "e9eb668fd6aa14b1e26436f429b5550108af0a1dfd443b8cea0bcb3ab3028fca"

ZENZ_REVISION = "ef3e66e3665fe6a19598e6578e9b88a994b647fd"
ZENZ_URL = (
    "https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/resolve/"
    f"{ZENZ_REVISION}/ggml-model-Q5_K_M.gguf"
)
ZENZ_SHA256 = "29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673"


def katakana_to_hiragana(text: str) -> str:
  """Converts katakana (U+30A1..U+30F6) to hiragana and leaves the rest."""
  return "".join(
      chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text
  )


def zenz_request_line(request_id: int, left_context: str, reading_katakana: str) -> str:
  """Formats a scorer request as floatype/scorer/protocol.h's FormatRequest does."""
  def field(text: str) -> str:
    return text.replace("\t", " ").replace("\r", "").replace("\n", "")
  return f"{request_id}\t{field(left_context)}\t{field(reading_katakana)}\n"


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


def latency_summary(milliseconds: list[float]) -> dict:
  ordered = sorted(milliseconds)

  def pct(p: float) -> float:
    return ordered[min(len(ordered) - 1, int(round(p * (len(ordered) - 1))))]

  return {
      "mean_ms": statistics.fmean(ordered),
      "p50_ms": pct(0.50),
      "p90_ms": pct(0.90),
      "p99_ms": pct(0.99),
      "max_ms": ordered[-1],
  }


def fetch(url: str, name: str, sha256: str) -> Path:
  """Downloads url into the cache once and verifies its SHA-256 every time."""
  path = CACHE_DIR / name
  if not path.exists():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(path.suffix + ".partial")
    with urllib.request.urlopen(url, timeout=120) as response:
      partial.write_bytes(response.read())
    partial.rename(path)
  digest = hashlib.sha256(path.read_bytes()).hexdigest()
  if digest != sha256:
    raise SystemExit(f"unexpected sha256 {digest} for {path}")
  return path


def convert_with_mozc(readings: list[str], binary: Path, data_file: Path) -> list[str]:
  """Returns Mozc's top-1 conversion for each hiragana reading, in order."""
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


def convert_with_zenz(
    pairs: list[tuple[str, str]], binary: Path, model: Path, threads: int
) -> tuple[list[tuple[str, float, str]], dict]:
  """Returns (output, milliseconds, stop reason) per (left context, reading), plus the scorer's stats.

  Requests go one at a time because the scorer skips requests superseded by newer ones.
  """
  scorer = subprocess.Popen(
      [str(binary), "--model", str(model), "--threads", str(threads)],
      stdin=subprocess.PIPE,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      encoding="utf-8",
      errors="replace",  # A length cap can cut a character in half.
  )
  results = []
  for request_id, (context, reading) in enumerate(pairs):
    scorer.stdin.write(zenz_request_line(request_id, context, reading))
    scorer.stdin.flush()
    answer_id, text, stop, microseconds = scorer.stdout.readline().rstrip("\n").split("\t")
    if int(answer_id) != request_id:
      raise SystemExit(f"expected an answer to request {request_id}, got {answer_id}")
    results.append((text, int(microseconds) / 1000, stop))
  _, stderr = scorer.communicate()
  if scorer.returncode != 0:
    raise SystemExit(f"floatype_scorer exited with {scorer.returncode}: {stderr}")
  return results, json.loads(stderr.strip().splitlines()[-1])


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--engine", choices=["mozc", "zenz"], default="mozc")
  parser.add_argument(
      "--binary", type=Path, help="default: the engine's tool under bazel-bin or floatype/scorer/build"
  )
  parser.add_argument(
      "--data_file", type=Path, default=Path("bazel-bin/data_manager/oss/mozc.data")
  )
  parser.add_argument("--no_context", action="store_true", help="zenz: ignore the left context")
  parser.add_argument("--threads", type=int, default=4, help="zenz: inference threads")
  parser.add_argument("--out", type=Path, help="write per-item results as JSON lines")
  args = parser.parse_args()

  items = json.loads(fetch(AJIMEE_URL, f"ajimee-v1-{AJIMEE_COMMIT[:12]}.json", AJIMEE_SHA256).read_bytes())
  report = {"engine": args.engine}
  extras: list[dict] = [{} for _ in items]
  if args.engine == "mozc":
    binary = args.binary or Path("bazel-bin/converter/quality_regression_main")
    top1 = convert_with_mozc(
        [katakana_to_hiragana(item["input"]) for item in items], binary, args.data_file
    )
  else:
    binary = args.binary or Path("floatype/scorer/build/floatype_scorer")
    model = fetch(ZENZ_URL, f"zenz-v3.2-small-{ZENZ_REVISION[:12]}-Q5_K_M.gguf", ZENZ_SHA256)
    pairs = [("" if args.no_context else item.get("context_text", ""), item["input"]) for item in items]
    results, stats = convert_with_zenz(pairs, binary, model, args.threads)
    top1 = [text for text, _, _ in results]
    extras = [{"ms": ms, "stop": stop} for _, ms, stop in results]
    report.update(stats, binary=str(binary), context=not args.no_context)
    report["latency"] = latency_summary([ms for _, ms, _ in results])
    report["stops"] = {s: sum(e["stop"] == s for e in extras) for s in sorted({e["stop"] for e in extras})}

  rows = [score(item, t) | extra for item, t, extra in zip(items, top1, extras)]
  if args.out:
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows),
        encoding="utf-8",
    )
  report["accuracy"] = summarize(rows)
  json.dump(report, sys.stdout, indent=2)
  print()


if __name__ == "__main__":
  main()
