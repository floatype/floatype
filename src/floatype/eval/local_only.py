"""Checks that floatype works without the network and never tries to use it.

1. Runs Mozc's real engine and the real zenz scorer (replay_main) inside
   macOS's sandbox with a rule that kills the process on any network access,
   and expects conversions and zenz's corrections to come out as usual. curl
   under the same rule is the control: it must be killed.
2. Lists the networking functions and frameworks each executable links (the
   scorer and, with --pkg, everything the installer puts in place), and fails
   on any outside a short list of known, local uses.
3. Lists the executables that name the one address floatype may ask, the
   latest release on GitHub. The settings dialog asks it through curl when
   the user presses its update button, which no symbol shows; more than one
   executable holding it fails.

Run from src/ after building the scorer (floatype/scorer/CMakeLists.txt) and
  bazelisk build --config release_build //floatype/eval:replay_main
  python3 floatype/eval/local_only.py [--pkg bazel-bin/mac/Mozc.pkg]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

# Killed on the spot, so a single attempt fails the run.
PROFILE = "(version 1)(allow default)(deny network* (with send-signal SIGKILL))"
# subprocess reports a process killed by a signal as minus the signal.
SIGKILL_EXIT = -9

# Undefined symbols that reach for the network.
NETWORK_SYMBOL = re.compile(
    r"^_(getaddrinfo|gethostbyname2?|connect|socket|sendto|recvfrom|bind|listen"
    r"|NSURLSession\w*|CFSocketCreate\w*|CFStreamCreatePairWithSocketToHost"
    r"|curl_\w+|nw_\w+|SCNetworkReachability\w*)$"
)
NETWORK_LIBRARY = re.compile(r"CFNetwork|/Network\.framework|libcurl")
# Known local uses: Qt's event loop watches file descriptors through CFSocket.
ALLOWED = {
    ("QtCore", "_CFSocketCreateWithNative"),
    ("QtCore", "_CFSocketCreateRunLoopSource"),
}

# (left context, reading) pairs where zenz is known to correct Mozc.
SENTENCES = [("熱が", "きかい"), ("今日は", "いしゃにいく"), ("", "わたしはがくせいです")]


def network_uses(name, undefined_symbols, libraries):
  """The network symbols and libraries in one executable, minus known ones."""
  found = [s for s in undefined_symbols if NETWORK_SYMBOL.match(s)]
  found += [l for l in libraries if NETWORK_LIBRARY.search(l)]
  return [f for f in found if (name, f) not in ALLOWED]


def mach_o_files(root):
  for directory, _, files in os.walk(root):
    for file in files:
      path = os.path.join(directory, file)
      if os.path.islink(path) or not os.access(path, os.X_OK):
        continue
      with open(path, "rb") as f:
        magic = f.read(4)
      if magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
        yield path


def static_check(paths):
  failures = []
  for path in paths:
    symbols = subprocess.run(["nm", "-u", path], capture_output=True,
                             text=True).stdout.split()
    libraries = subprocess.run(["otool", "-L", path], capture_output=True,
                               text=True).stdout.split()
    uses = network_uses(os.path.basename(path), symbols, libraries)
    print(f"{os.path.basename(path)}: {' '.join(uses) or 'なし'}")
    if uses:
      failures.append((path, uses))
  return failures


# The update button's address (floatype/update/update_check.h).
UPDATE_ADDRESS = b"api.github.com/repos/floatype/floatype/releases/latest"


def update_askers(paths):
  """The executables that hold the address the update button asks."""
  askers = []
  for path in paths:
    with open(path, "rb") as f:
      if UPDATE_ADDRESS in f.read():
        askers.append(path)
  return askers


def sandboxed_replay(replay, scorer):
  lines = "".join(f"{left}\t{reading}\n" for left, reading in SENTENCES)
  done = subprocess.run(
      ["sandbox-exec", "-p", PROFILE, replay, f"--scorer={scorer}"],
      input=lines, capture_output=True, text=True)
  if done.returncode != 0:
    return f"replay_main が終了コード {done.returncode} で止まった（{SIGKILL_EXIT} ならネットワークに触れて止められた）"
  rows = [line.split("\t") for line in done.stdout.splitlines()]
  if len(rows) != len(SENTENCES):
    return f"結果が {len(rows)} 行しかない"
  corrected = sum(1 for row in rows if row[0] != row[1])
  for row in rows:
    print(f"  Mozc: {row[0]} → 表示: {row[1]}")
  if corrected == 0:
    return "zenz が一度も補正しなかった（採点プロセスが動いていない）"
  return None


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--replay", default="bazel-bin/floatype/eval/replay_main")
  parser.add_argument("--scorer", default="floatype/scorer/build/floatype_scorer")
  parser.add_argument("--pkg", help="installer package whose payload to check")
  args = parser.parse_args()

  failed = False
  control = subprocess.run(
      ["sandbox-exec", "-p", PROFILE, "/usr/bin/curl", "-s", "-m", "2",
       "http://127.0.0.1:9"], capture_output=True)
  if control.returncode != SIGKILL_EXIT:
    print(f"対照: curl が止められなかった（終了コード {control.returncode}）。囲いが効いていない")
    failed = True

  print("ネットワークを禁じた囲いの中で変換する:")
  error = sandboxed_replay(args.replay, args.scorer)
  if error:
    print(f"失敗: {error}")
    failed = True

  print("実行ファイルが使うネットワークの関数:")
  paths = [args.scorer]
  with tempfile.TemporaryDirectory() as temp:
    if args.pkg:
      expanded = os.path.join(temp, "pkg")
      subprocess.run(["pkgutil", "--expand-full", args.pkg, expanded],
                     check=True, capture_output=True)
      paths += list(mach_o_files(expanded))
    if static_check(paths):
      failed = True
    askers = update_askers(paths)
    print("更新の確認先を持つ実行ファイル（設定画面の 1 つだけのはず）: "
          + (" ".join(os.path.basename(a) for a in askers) or "なし"))
    if len(askers) > 1:
      failed = True

  print("失敗" if failed else "ローカルで完結している")
  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
