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

log="$HOME/Library/Logs/floatype-update.log"
echo "floatype: ビルドしています（変更が無ければ数秒、初回は数分。記録は ${log}）"
# Only errors are worth reading here; the full output goes to the log.
build() {
  nix develop .. --command bash -c '
  set -euo pipefail
  cmake -S floatype/scorer -B floatype/scorer/build -DCMAKE_BUILD_TYPE=Release
  cmake --build floatype/scorer/build -j --target floatype_scorer zenz_repacked
  # The dictionaries made from outside data: downloaded once, then cached, and
  # made again only when their inputs change.
  python3 floatype/tools/make_extra_dictionary.py
  # Warnings come from upstream Mozc and Xcode; errors still show.
  bazelisk build --config release_build --define=floatype_zenz=bundle \
    --output_filter=DONT_MATCH_ANYTHING package
  ' >"$log" 2>&1
}
# A Bazel server left from before a logout cannot read the new login's
# environment ("Failed to initialize sandbox: getconf failed"): stop it and
# build once more.
built=0
build || built=$?
if [ "$built" -ne 0 ] && grep -q "Failed to initialize sandbox" "$log"; then
  echo "floatype: 前のログインのビルドサーバーが残っていたので、止めてやり直します"
  nix develop .. --command bazelisk shutdown >/dev/null 2>&1 || true
  built=0
  build || built=$?
fi
if [ "$built" -ne 0 ]; then
  errors="$(grep -E "error:|ERROR:|Error|Traceback" "$log" | tail -20 || true)"
  echo "${errors:-$(tail -30 "$log")}"
  echo "floatype: ビルドに失敗しました。全体は ${log} にあります。"
  exit 1
fi

# What macOS reads once when it registers the input method: the Info.plist,
# its localized names and the menu bar icons.
registered() {
  cat "$plist" "$app"/Contents/Resources/*.lproj/InfoPlist.strings \
    "$app"/Contents/Resources/*.tiff 2>/dev/null | shasum || true
}
before="$(registered)"
# The package always asks for a logout; whether one is needed is decided below.
echo "floatype: インストールしています"
if ! out="$(sudo installer -pkg bazel-bin/mac/Mozc.pkg -target / 2>&1)"; then
  echo "$out"
  exit 1
fi
after="$(registered)"

if [ -z "$before" ] || [ "${before%% *}" != "${after%% *}" ]; then
  echo "floatype: 入力メソッドの登録内容（名前やアイコンを含む）が変わったので、ログアウトしてログインし直してください。"
else
  # launchd keeps the old binaries' code hashes for its agents and refuses to
  # start the new ones (EX_CONFIG) until they are registered again. macOS
  # restarts the input method on the next key; the scorer exits with the
  # converter.
  for agent in Converter Renderer; do
    label="io.github.floatype.inputmethod.Japanese.$agent"
    launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
    # bootout returns before launchd lets go of the agent; bootstrapping too
    # soon fails with "5: Input/output error".
    for try in 1 2 3 4 5 6 7 8 9 10; do
      launchctl bootstrap "gui/$(id -u)" "/Library/LaunchAgents/$label.plist" 2>/dev/null && break
      if [ "$try" = 10 ]; then
        echo "floatype: $agent を登録し直せませんでした。ログアウトしてログインし直してください。"
      fi
      sleep 0.5
    done
  done
  killall floatype 2>/dev/null || true
  echo "floatype: 更新しました。ログアウトは不要です（入力中の文字があれば確定してから使ってください）。"
fi
