# Floatype

Apple Silicon の Mac 用の日本語入力です。Mozc の辞書変換をもとに、打つのを止めたところで小さな言語モデル zenz（zenz-v3.2-small）が変換を見直します。変換も zenz もすべて Mac の中で動き、ネットワークには触れません。

- 動きの説明: [docs/floatype/behavior.md](docs/floatype/behavior.md)（打ってから確定するまで、何 ms 後に何が起きるか）
- 仕様と計測: [docs/floatype/spec.md](docs/floatype/spec.md)

## 動く環境

- Apple Silicon（M1 以降）の Mac。Intel の Mac では動きません。
- macOS 12 以降の作りです。動きを確かめているのは macOS 27 です。
- メモリは 16GB 以上を想定しています。floatype 全体で、打っている間は約 72MB、しばらく打たないと約 39MB 使います（M4 Pro で、アクティビティモニタの「メモリ」の数え方）。

## 入れ方

1. [Releases](https://github.com/floatype/floatype/releases) から、`floatype-<版>.pkg` と `floatype-<版>.pkg.sha256` の 2 つを同じフォルダに落とします。
2. ターミナルでそのフォルダに移り、ファイルが壊れたり差し替えられたりしていないか確かめます。

   ```sh
   shasum -a 256 -c floatype-<版>.pkg.sha256
   ```

   `floatype-<版>.pkg: OK` と出れば、Releases に置いたものと同じです。`FAILED` と出たら開かずに消してください。
3. pkg をダブルクリックして、インストーラの案内に従います。開けなかったときは、次の「macOS に止められたとき」を見てください。
4. 終わったらログアウトして、ログインし直します。
5. システム設定 → キーボード → 入力ソース の「編集…」で「＋」を押し、日本語の「Floatype」を足します。

設定は `~/.config/floatype/config.toml` にあります。入力メニューの「環境設定…」で変えても（F の印が Floatype で足した項目です）、このファイルを手で書き換えてもかまいません。設定画面は変えた行だけを書き換えるので、手で書いたコメントは残ります（例は [src/floatype/config/config.example.toml](src/floatype/config/config.example.toml)）。

### macOS に止められたとき

floatype の pkg には Apple の署名と公証がありません。そのため macOS が「開けません」と止めることがあります。上の手順 2 で `OK` を確かめた pkg に限り、その 1 つだけを次の手順で許可してください。

1. 止められたダイアログは「完了」で閉じます。
2. システム設定 → プライバシーとセキュリティ を開き、下の方の「セキュリティ」を見ます。
3. 「“floatype-<版>.pkg” は Mac を保護するためにブロックされました」の横の「このまま開く」を押します。
4. もう一度出る確認で、ファイル名が自分の落としたものと同じかを見てから「このまま開く」を押します。

文言や場所は macOS の版で少し違います。Gatekeeper そのものを切る必要はありません。「マルウェアが含まれている」「壊れている」と出た場合は、許可せずにファイルを消してください。Apple の説明: https://support.apple.com/ja-jp/102445

## キーの使い方

キーはことえり配列のものです。

| キー | いつ | 動き |
| --- | --- | --- |
| Tab か ↓ | 打っている途中に予測候補が出ているとき | 予測候補を番号つきの一覧にして選ぶ。数字か矢印で選び、Enter で確定する |
| Space | 打っている途中 | 変換する。もう一度押すと次の候補 |
| control + z | 打っている途中 | 変換して、すぐ zenz に聞く |
| control + z | 変換が出ているとき | 「これじゃない」。今の答えを外して、zenz の次の答えを出す。押すたびに次へ進む |
| control + delete | 一覧で候補を選んでいるとき | 入力の履歴から出た候補を消す（辞書の語は消えない）。zenz の補正が出ているときは、その補正を止め、同じ読みでは二度としない |
| delete | 確定した直後 | 文字を消すのに加えて、その確定で覚えたことを取り消す |
| かな / 英数 | 打っている途中 | ひらがなと英字を切り替える（打った文字は残る） |
| / | 英字や数字のすぐ後 | 半角の / のまま入る（2026/11/20、確定した英単語のすぐ後）。日英混在入力をオンにしていれば、打っている途中の英単語の後でも効く（before/after、test/spec）。かなの後は今までどおり ・。いつでも ・ を打つには z/。ローマ字入力のときだけ。設定画面の「一般」タブの「入力の補助」で切れる |

打っている途中に出る小さな窓（予測候補）には、下の帯がありません。Tab か ↓ で開く予測候補の一覧と、Space で開く変換候補には、帯の右に「1 / 9」が出ます。帯の左の丸の色で、どちらの一覧かが分かります。予測候補の一覧は青、変換候補は灰色です。丸は設定画面の「外観」で消せます（`candidate_kind_mark`）。

## 更新

設定画面の「その他」タブで「更新を確認」を押します。新しい版があれば、ダウンロードして macOS のインストーラで開きます。Floatype が外と通信するのは、このボタンを押したときだけです。

## 消し方

`/Applications/floatype/Uninstallfloatype.app` を開きます。

## 自分でビルドする

Xcode と Nix が要ります。`src/floatype/update.sh` が、採点プロセスと zenz のモデル、足した辞書を用意してビルドし、この Mac に入れ直します。

## ライセンス

floatype は Mozc（BSD 3-Clause）の派生です。同梱するもの（zenz-v3.2-small は CC BY-SA 4.0、llama.cpp は MIT、足した辞書の出どころなど）のライセンスは [src/data/installer/credits_en.html](src/data/installer/credits_en.html) にあります。

---

以下は上流の Mozc の README です。

[Mozc - a Japanese Input Method Editor designed for multi-platform](https://github.com/google/mozc)
===================================

Copyright 2010-2026 Google LLC

Mozc is a Japanese Input Method Editor (IME) designed for multi-platform such as
Android OS, Apple macOS, Chromium OS, GNU/Linux and Microsoft Windows.  This
OpenSource project originates from
[Google Japanese Input](http://www.google.com/intl/ja/ime/).

Mozc is not an officially supported Google product.

Build Status
------------

| Linux | Windows | macOS | Android lib |
|:-----:|:-------:|:-----:|:-----------:|
| [![Linux](https://github.com/google/mozc/actions/workflows/linux.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/linux.yaml) | [![Windows](https://github.com/google/mozc/actions/workflows/windows.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/windows.yaml) | [![macOS](https://github.com/google/mozc/actions/workflows/macos.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/macos.yaml) | [![Android lib](https://github.com/google/mozc/actions/workflows/android.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/android.yaml) |


What's Mozc?
------------
For historical reasons, the project name *Mozc* has two different meanings:

1. Internal code name of Google Japanese Input that is still commonly used
   inside Google.
2. Project name to release a subset of Google Japanese Input in the form of
   source code under OSS license without any warranty nor user support.

In this repository, *Mozc* means the second definition unless otherwise noted.

Detailed differences between Google Japanese Input and Mozc are described in [About Branding](docs/about_branding.md).

For policies on vocabulary and conversion results, see
[Vocabulary Policy](VOCABULARY_POLICY.md).

Build Instructions
------------------

* [How to build Mozc for Android](docs/build_mozc_for_android.md): for Android library (`libmozc.so`)
* [How to build Mozc for Linux](docs/build_mozc_for_linux.md): for Linux desktop
* [How to build Mozc for macOS](docs/build_mozc_in_osx.md): for macOS build
* [How to build Mozc for Windows](docs/build_mozc_in_windows.md): for Windows

Release Plan
------------

tl;dr. **There is no stable version.**

As described in [About Branding](docs/about_branding.md) page, Google does
not promise any official QA for OSS Mozc project.  Because of this,
Mozc does not have a concept of *Stable Release*.  Instead we change version
number every time when we introduce non-trivial change.  If you are
interested in packaging Mozc source code, or developing your own products
based on Mozc, feel free to pick up any version.  They should be equally
stable (or equally unstable) in terms of no official QA process.

[Release History](docs/release_history.md) page may have additional
information and useful links about recent changes.

License
-------

All Mozc code written by Google is released under
[The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause).
For third party code under [src/third_party](src/third_party) directory,
see each sub directory to find the copyright notice.  Note also that
outside [src/third_party](src/third_party) following directories contain
third party code.

### [src/data/dictionary_oss/](src/data/dictionary_oss)
Mixed.
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/dictionary/](src/data/test/dictionary)
The same as [src/data/dictionary_oss/](src/data/dictionary_oss).
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/stress_test/](src/data/test/stress_test)
Public Domain.  See the comment in
[src/data/test/stress_test/sentences.txt](src/data/test/stress_test/sentences.txt)
