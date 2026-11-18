#!/bin/bash
# Builds floatype with the scorer and the zenz model, installs it, and restarts
# its processes. Says when a logout is needed instead (the input method's
# Info.plist changed, so macOS has to register it again).
#
# Usage, from anywhere: path/to/floatype/src/floatype/update.sh
set -euo pipefail

src="$(cd "$(dirname "$0")/.." && pwd)"
cd "$src"
app="/Library/Input Methods/floatype.app"
plist="$app/Contents/Info.plist"

nix develop .. --command bash -c '
  set -euo pipefail
  cmake -S floatype/scorer -B floatype/scorer/build -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build floatype/scorer/build -j --target floatype_scorer >/dev/null
  # Here Bazel does not notice edited headers (protocol buffers, keymaps and
  # hand-written .h files alike) until a clean build, and mixing old and new
  # objects crashes the converter. A clean build takes a few minutes.
  bazelisk clean
  bazelisk build --config release_build --define=floatype_zenz=bundle package
'

before="$(shasum "$plist" 2>/dev/null || true)"
# The package always asks for a logout; whether one is needed is decided below.
sudo installer -pkg bazel-bin/mac/Mozc.pkg -target / | grep -v "requires logging out" || true
after="$(shasum "$plist" 2>/dev/null || true)"

if [ -z "$before" ] || [ "${before%% *}" != "${after%% *}" ]; then
  echo "floatype: 入力メソッドの登録内容が変わったので、ログアウトしてログインし直してください。"
else
  # launchd keeps the old binaries' code hashes for its agents and refuses to
  # start the new ones (EX_CONFIG) until they are registered again. macOS
  # restarts the input method on the next key; the scorer exits with the
  # converter.
  for agent in Converter Renderer; do
    label="io.github.floatype.inputmethod.Japanese.$agent"
    launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
    launchctl bootstrap "gui/$(id -u)" "/Library/LaunchAgents/$label.plist"
  done
  killall floatype 2>/dev/null || true
  echo "floatype: 更新しました。ログアウトは不要です（入力中の文字があれば確定してから使ってください）。"
fi
