# Floatype

Apple Silicon の Mac 用の日本語入力です。Space を押さなくても、打つのを止めたところで変換します。そのあと、小さな言語モデル zenz（zenz-v3.2-small）が直前の文を見て、変換を見直します。

変換も zenz も Mac の中で動きます。外と通信するのは、設定画面で「更新を確認」を押したときだけです。

Mozc（Google 日本語入力のオープンソース版）をもとに作っています。

## できること

- 打つのを止めると、すぐに変換が出ます（ライブ変換）。Space で候補を選び直せます。
- 打つのを止めて少したつと、zenz が変換を見直します。読みと直前の文を見て、合わない変換を直します。待ち時間は設定で変えられます。
- 打っている途中に予測候補が出ます。Tab か ↓ で選べます。
- 英字と数字は、最初から半角で入ります（v1.2.3、2026）。
- 設定でオンにすると、日本語の途中で打った英単語を英字のまま残します（「きょうは update する」が「今日はupdateする」）。
- zenz が直した変換を退けると、それを覚えて次から出しません。覚えた内容は設定画面の「学習データを見る…」で見て消せます。
- 設定は 1 つのテキストファイルにあり、手で書き換えられます。ユーザー辞書も同じです。

動きの細かい説明は [docs/floatype/behavior.md](docs/floatype/behavior.md)、仕様と計測は [docs/floatype/spec.md](docs/floatype/spec.md) にあります。

## 動く環境

- Apple Silicon（M1 以降）の Mac。Intel の Mac では動きません。
- macOS 12 以降の作りです。動きを確かめているのは macOS 27 です。
- メモリは 16GB 以上を想定しています。Floatype 全体で、打っている間は約 72MB、しばらく打たないと約 39MB 使います（M4 Pro で、アクティビティモニタの「メモリ」の数え方）。

## 入れ方

1. [Releases](https://github.com/floatype/floatype/releases) から、`floatype-<バージョン>.pkg` と `floatype-<バージョン>.pkg.sha256` の 2 つを同じフォルダに落とします。
2. ターミナルでそのフォルダに移り、ファイルが壊れたり差し替えられたりしていないか確かめます。

   ```sh
   shasum -a 256 -c floatype-<バージョン>.pkg.sha256
   ```

   `floatype-<バージョン>.pkg: OK` と出れば、Releases に置いたものと同じです。`FAILED` と出たら開かずに消してください。
3. pkg をダブルクリックして、インストーラの案内に従います。開けなかったときは、次の「macOS に止められたとき」を見てください。
4. 終わったらログアウトして、ログインし直します。
5. システム設定 → キーボード → 入力ソース の「編集…」で「＋」を押し、日本語の「Floatype」を足します。

入力ソースは「Floatype」の 1 つだけです。英字を打つときは、英数キーで macOS の「ABC」に切り替えます。かなキーで Floatype に戻ります。

### macOS に止められたとき

Floatype の pkg には Apple の署名と公証がありません。そのため macOS が「開けません」と止めることがあります。上の手順 2 で `OK` を確かめた pkg に限り、その 1 つだけを次の手順で許可してください。

1. 止められたダイアログは「完了」で閉じます。
2. システム設定 → プライバシーとセキュリティ を開き、下の方の「セキュリティ」を見ます。
3. 「“floatype-<バージョン>.pkg” は Mac を保護するためにブロックされました」の横の「このまま開く」を押します。
4. もう一度出る確認で、ファイル名が自分の落としたものと同じかを見てから「このまま開く」を押します。

文言や場所は macOS のバージョンで少し違います。Gatekeeper そのものを切る必要はありません。「マルウェアが含まれている」「壊れている」と出た場合は、許可せずにファイルを消してください。Apple の説明: https://support.apple.com/ja-jp/102445

## キーの使い方

キーはことえり配列のものです。

| キー | いつ | 動き |
| --- | --- | --- |
| Tab か ↓ | 打っている途中に予測候補が出ているとき | 予測候補を番号つきの一覧にして選ぶ。数字か矢印で選び、Enter で確定する |
| Space | 打っている途中 | 変換する。もう一度押すと次の候補 |
| Enter | 打っている途中、変換が出ているとき | 画面に出ているとおりに確定する |
| control + z | 打っている途中 | 変換して、すぐ zenz に聞く |
| control + z | 変換が出ているとき | 「これじゃない」。今の答えを外して、zenz の次の答えを出す。押すたびに次へ進む |
| control + delete | 一覧で候補を選んでいるとき | 入力の履歴から出た候補を消す（辞書の語は消えない）。zenz の補正が出ているときは、その補正を止め、同じ読みでは二度としない |
| control + delete | 確定した直後 | 確定を取り消して、打っている途中に戻す |
| delete | 確定した直後 | 文字を消すのに加えて、その確定で覚えたことを取り消す |
| 英数 | 何も打っていないとき | macOS の「ABC」に切り替える。かなキーで Floatype に戻る |
| かな / 英数 | 打っている途中 | ひらがなと英字を切り替える（打った文字は残る） |

打っている途中に出る小さな窓（予測候補）には、下の帯がありません。Tab か ↓ で開く予測候補の一覧と、Space で開く変換候補には、帯の右に「1 / 9」が出ます。帯の左の丸の色で、どちらの一覧かが分かります。予測候補の一覧は青、変換候補は灰色です。丸は設定画面の「外観」で消せます（`candidate_kind_mark`）。

### / の打ち分け

英字や数字のすぐ後の / は、半角の / のまま入ります（2026/11/20）。確定した英単語のすぐ後でも同じです。かなの後は今までどおり ・ になります。いつでも ・ を打つには z/ と打ちます。

日英混在入力をオンにしていれば、打っている途中の英単語の後でも / のまま入ります（before/after、test/spec）。

この打ち分けはローマ字入力のときだけ効きます。設定画面の「一般」ページの「入力の補助」で切れます。

## 設定

入力メニューの「環境設定…」で変えます。F の印が付いた項目が、Floatype で足したものです。

設定は `~/.config/floatype/config.toml` に保存されます。このファイルを手で書き換えてもかまいません。設定画面は変えた行だけを書き換えるので、手で書いたコメントは残ります。書き方の例は [src/floatype/config/config.example.toml](src/floatype/config/config.example.toml) にあります。

### 自分で書き換えられるファイル

設定のほかに、登録した語と zenz が覚えたことも、テキストファイルにあります。どれも手で書き換えられ、保存して 1 秒ほどで反映されます。

| 中身 | ファイル | 画面 |
| --- | --- | --- |
| 設定 | `~/.config/floatype/config.toml` | 入力メニューの「環境設定…」 |
| 単語登録の語 | `~/.config/floatype/user_dictionary.tsv` | 入力メニューの「単語登録…」と「辞書ツール…」 |
| 英字のまま残す英単語 | `~/.config/floatype/english_words.txt` | 設定画面の「一般」ページの「入力の補助」にある「英単語を編集…」。初めからある語も見られます。手で書くときは 1 行に 1 語で、頭に `-` を付けた語は外します |
| zenz が覚えたこと | `floatype_zenz_*.tsv` の 4 つ | 設定画面の「入力補助」ページの「学習データを見る…」。「Finder で表示」でファイルの場所が開きます |

zenz が覚えるのは次の 4 種類です。「学習データを見る…」では読みか表記で絞り込み、選んだ行か全部を消せます。

| 種類 | 覚えるとき |
| --- | --- |
| 退けた語 | zenz が出した語を、別の候補に選び直したとき。下の「1 回退けた語」を、もう一度退けたとき |
| 1 回退けた語 | Space で Mozc の結果に戻して、そのまま確定したとき。Space の押し間違い 1 回では止めないための控えです |
| すぐ出す補正 | zenz の補正をそのまま確定したとき |
| 止めた補正 | 補正が出ているときに control + delete で止めたとき |

Mozc が覚えた候補の順は、この一覧には出ません。1 つずつ消すには、その語を変換して control + delete を押します。

### 日本語の途中の英単語

日英混在入力（`mixed_english`）は、初めはオフです。設定画面の「一般」ページの「入力の補助」でオンにします。

オンにすると、日本語の途中で打った英単語が英字のまま残ります。インジケータの見た目は変わりません。そのすぐ下の「インジケータの右端に、いまの語が日本語か英字かを出す」もオンにすると、インジケータの右端に、キャレットの直前の語を日本語（あ）と英字（A）のどちらで扱っているかが出ます。

| 打つキー | 結果 |
| --- | --- |
| `kyouhaupdatesuru`（きょうは update する） | 今日はupdateする |
| `update` のあとに Space | 「update 」と確定して、半角の空白が入る |
| `config.toml` のあとに Enter | config.toml |

空白で区切って打つ英文（nice to meet you）は扱いません。最初の語が日本語に変換されます。英文は英数キーで「ABC」に切り替えて打ってください。

## 更新

設定画面の「その他」ページで「更新を確認」を押します。新しいバージョンがあれば、ダウンロードして macOS のインストーラで開きます。Floatype が外と通信するのは、このボタンを押したときだけです。

## 消し方

`/Applications/floatype/Uninstallfloatype.app` を開きます。

## 困ったとき

不具合や要望は [Issues](https://github.com/floatype/floatype/issues) に書いてください。打ったキーと、出てほしかった文字、実際に出た文字があると調べやすくなります。

## 自分でビルドする

Xcode と Nix が要ります。`src/floatype/update.sh` が、採点プロセスと zenz のモデル、足した辞書を用意してビルドし、この Mac に入れ直します。

## ライセンス

Floatype は Mozc（BSD 3-Clause）の派生です。同梱するもの（zenz-v3.2-small は CC BY-SA 4.0、llama.cpp は MIT、足した辞書の出どころなど）のライセンスは [src/data/installer/credits_en.html](src/data/installer/credits_en.html) にあります。

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
