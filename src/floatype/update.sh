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
  # Bazel does not rebuild the embedded keymaps when data/keymap changes, so the
  # object is always rebuilt.
  find bazel-out/ -path "*_objs/config_file_stream/config_file_stream.o" 2>/dev/null |
    while read -r o; do
      d=$(dirname "$(dirname "$(dirname "$o")")")
      chmod u+w "$(dirname "$o")" "$d"
      rm -f "$o" "$d/libconfig_file_stream.a"
    done
  bazelisk build --config release_build --define=floatype_zenz=bundle package
'

before="$(shasum "$plist" 2>/dev/null || true)"
sudo installer -pkg bazel-bin/mac/Mozc.pkg -target /
after="$(shasum "$plist" 2>/dev/null || true)"

if [ -z "$before" ] || [ "${before%% *}" != "${after%% *}" ]; then
  echo "floatype: 入力メソッドの登録内容が変わったので、ログアウトしてログインし直してください。"
else
  # launchd restarts the converter and the renderer; macOS restarts the input
  # method on the next key. The scorer exits with the converter.
  killall floatype floatypeConverter floatypeRenderer 2>/dev/null || true
  echo "floatype: 更新しました。ログアウトは不要です（入力中の文字があれば確定してから使ってください）。"
fi
