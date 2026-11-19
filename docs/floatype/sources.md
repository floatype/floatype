# 出典

[spec.md](spec.md) が引用している数値と、その出典です。調査日は 2026-11-18 です。精度と速度の値の多くは開発者自身による計測で、第三者による再現はほとんどありません。

## 速さ

| 内容 | 出典 |
| --- | --- |
| 打鍵の表示に足す遅延の予算（p50 5ms、p99 16.7ms）は、表示の1フレーム（120Hz で 8.3ms、60Hz で 16.7ms）と、次の2つの計測からの導出値 | 下の2行 |
| 遅延が知覚される閾値は、直接タップで 69ms、間接タップで 96ms（タッチ面での計測） | [How Much Faster is Fast Enough?（CHI 2015）](https://www.tactuallabs.com/papers/howMuchFasterIsFastEnoughCHI15.pdf) |
| macOS のエディタやターミナルの打鍵から表示までの遅延は、最良で 29〜45ms（60Hz、カメラ計測） | [Terminal latency (2022)](https://www.lkhrs.com/blog/terminal-latency/) |
| 物理キーボードで遅延 20ms と 200ms を比べた対照実験（200ms で修正課題が遅くなる） | [Schmid ほか 2023](https://epub.uni-regensburg.de/55007/1/text-input-latency.pdf) |

## 変換モデル

| 内容 | 出典 |
| --- | --- |
| ニューラル変換の所要時間は、入力1文字あたり 26M で 3.5ms、91M で 9.8ms、310M で 26.4ms。辞書を下書きにした投機的デコードで 6.0〜6.9ms（M2 Pro、GPU） | [Zenzai 論文（言語処理学会 2025）](https://www.anlp.jp/proceedings/annual_meeting/2025/pdf_dir/P1-19.pdf) |
| zenz-v3.2-small は約 95M パラメータ、Q5_K_M で約 74MB。ライセンス表記は 2026-11-07 に Apache-2.0 から CC BY-SA 4.0 に変わった | [zenz-v3.2-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf) |
| zenz を動かすのに要るメモリは 150MB 程度 | [AzooKeyKanaKanjiConverter の Zenzai 文書](https://github.com/azooKey/AzooKeyKanaKanjiConverter/blob/main/Docs/zenzai.md) |

## 評価セット

| 内容 | 出典 |
| --- | --- |
| AJIMEE-Bench（200 問）。LICENSE ファイルは無く、README が元データ（日本語 Wikipedia 入力誤りデータセット v2）に従って CC BY-SA 3.0 と書く。`ajimee.py` は 401666cd の `JWTD_v2/v1/evaluation_items.json` を使う | [azooKey/AJIMEE-Bench](https://github.com/azooKey/AJIMEE-Bench) |
| 個人による拡張版（628 問）。上流には取り込まれていない。LICENSE ファイルは無く、README が Wikipedia 由来の問は CC BY-SA 3.0、新しく作った文（口語、ビジネス、技術、実運用の誤りから作った 146 問）は CC0 1.0 と書く。`ajimee.py --set extended` は c0783bba の `JWTD_v2/v2/evaluation_items.json` を使う。作者は `test.json` の問を調整に使わないよう勧めている | [nyanko3141592/AJIMEE-Bench](https://github.com/nyanko3141592/AJIMEE-Bench) |
| Anthy のコーパス（1,745 文）。ファイルの先頭に「copying: public domain」とある（リポジトリ全体は LGPLv3 以降、辞書は GPLv2）。`ajimee.py --set anthy` は 70c573d6 の `corpus/corpus.1.txt` を使う | [netsphere-labs/anthy](https://github.com/netsphere-labs/anthy/tree/master/corpus) |
| Anthy のコーパスで、AzooKeyKanaKanjiConverter 1,065、Google CGI API 1,104、zenz-v1 1,179 文が正解（2024-05） | [Zenzai の記事](https://zenn.dev/azookey/articles/ea15bacf81521e) |

## やらないことの理由

| 内容 | 出典 |
| --- | --- |
| OS 同梱の言語モデルは AJIMEE-Bench の 200 問で正解 0 問 | [Sumibi のベンチマーク](https://github.com/kiyoka/Sumibi/blob/main/benchmark/README.md) |
| OS 同梱の言語モデルの応答は、短い要求でも最短で約 320ms | 調査時の計測（Apple M4 Pro の Mac 1台、macOS 27.0.1） |
| 日本語の音声認識モデル（0.6B 級、量子化）の常駐メモリは約 0.7GB | [Haas-ja を Mac で動かした記事](https://zenn.dev/airtanker/articles/haas-ja-mac-dictation) |
| Core AI（Neural Engine を含む）の特殊化キャッシュは OS 更新で無効になる | [Apple の Core AI 文書](https://developer.apple.com/documentation/coreai/managing-model-specialization-and-caching) |

## Mozkey

| 内容 | 出典 |
| --- | --- |
| 既定値：ライブ変換の待ち 228ms、最小 2 文字、zenz の待ち 700ms、zenz を待って表示する方式の上限 250ms、ライブ変換は既定でオフ | [koyasi777/mozkey](https://github.com/koyasi777/mozkey) の README と `src/protocol/config.proto` |
| macOS 版の構成は Mozc 変換プロセス → Unix ソケット → 採点プロセス → HTTP（127.0.0.1）→ llama-server | 同リポジトリの `src/session/zenz_client_factory.cc` と `src/zenz_scorer/` |
| Mozc 本家との差分：`session.cc` が 2,933 行から 11,518 行、内容が違うファイル 582 | Mozkey の fa5a1bed と Mozc の 3cf2d3855 を比較（2026-11-18） |

## Mozc（このリポジトリ）

| 内容 | 該当箇所 |
| --- | --- |
| 変換プロセスと候補表示プロセスは LaunchAgent で常駐する | `src/mac/installer/LaunchAgents/` |
| macOS 版クライアントは、呼び戻しの遅延指定（`delay_millisec`）を見ずにすぐ送り返す | `src/mac/mozc_imk_input_controller.mm` の callback 処理 |
| 確定した文節を履歴として保持する数の既定は 4 | `src/converter/segments.cc` |
| 確定の取り消し（Undo）は Ctrl+Backspace に割り当てられ、取り消すと学習も戻す | `src/data/keymap/*.tsv`、`src/converter/converter.cc` の `RevertConversion` |
| 予測候補は既定でオン | `src/protocol/config.proto` の `use_history_suggest`、`use_dictionary_suggest` |
| 設定画面の保存先は `config1.db` | `src/config/config_handler.cc` |
| 入力先アプリの前後の文字を取得する処理がある | `src/mac/mozc_imk_input_controller.mm` の `fillSurroundingContext` |
| 利用統計、更新確認、クラッシュ送信のコードは無い。ブラウザで URL を開く経路はあるが、URL に値を入れるコードは無い | `src/` 全体の検索（2026-11-18） |
