// floatype: the settings pages after 一般, with the Qt screen's labels; what
// its labels held in parentheses is the gray line under the name.

import SwiftUI

/// Checkboxes for characters, in a row (確定する文字, 句読点変換).
struct CharacterBoxes: View {
  let items: [(String, Binding<Bool>)]

  var body: some View {
    HStack(spacing: 10) {
      ForEach(items.indices, id: \.self) { index in
        Toggle(items[index].0, isOn: items[index].1).toggleStyle(.checkbox)
      }
    }
  }
}

/// A row that is a part of the row above it, set in.
struct SubRow<Control: View>: View {
  let title: String
  var detail: String? = nil
  @ViewBuilder let control: Control

  var body: some View {
    Row(title: title, detail: detail) { control }
      .padding(.leading, 18)
  }
}

struct InputAssistGroup: View {
  @ObservedObject var store: SettingsStore

  var body: some View {
    let direct = store.bool("use_direct_commit").wrappedValue
    let english = store.bool("mixed_english").wrappedValue
    SettingsGroup(title: "入力の補助") {
      Row(title: "句読点・記号を打ったら、zenz を待ってその場で確定する") {
        Switch(isOn: store.bool("use_direct_commit"))
      }
      SubRow(title: "確定する文字") {
        CharacterBoxes(items: ["。", "、", "？", "！", "（", "）", "「", "」", "・"].map {
          ($0, store.contains("direct_commit_characters", Character($0)))
        })
      }.disabled(!direct)
      Row(title: "かなの入力中に打った英単語を英字のままにする",
          detail: "インジケータに あ / A を出す") {
        Switch(isOn: store.bool("mixed_english"))
      }
      SubRow(title: "英単語のあとの Space は、変換せずに確定して半角の空白を入れる") {
        Switch(isOn: store.bool("mixed_english_space_commits"))
      }.disabled(!english)
      SubRow(title: "英単語のあとすぐ Enter を押しても、英字で確定する", detail: "update") {
        Switch(isOn: store.bool("mixed_english_enter_commits"))
      }.disabled(!english)
      SubRow(title: "かなの無い入力は、頭の記号も打ったとおりにする", detail: "/review、--help") {
        Switch(isOn: store.bool("mixed_english_leading_symbols"))
      }.disabled(!english)
      Row(title: "英字や数字のすぐ後の / を、・ ではなく / のまま打つ",
          detail: "2026/11/20、test/spec") {
        Switch(isOn: store.bool("slash_after_ascii"))
      }
      Row(title: "長い規則の途中のローマ字を、いまの かな で見せる", detail: "n を ん で") {
        Switch(isOn: store.bool("display_ambiguous_romaji"))
      }
    }
  }
}

struct DictionaryPage: View {
  @ObservedObject var store: SettingsStore
  @State private var confirmClear = false

  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "学習") {
        Row(title: "学習機能") {
          Choice(selection: store.choice("history_learning_level"),
                 items: [("DEFAULT_HISTORY", "有効"), ("READ_ONLY", "有効（新規学習はしない）"),
                         ("NO_HISTORY", "無効")])
        }
        Row(title: "学習履歴のクリア") {
          Button("消す…") { confirmClear = true }
        }
      }
      SettingsGroup(title: "ユーザー辞書") {
        Row(title: "ユーザ辞書の編集", detail: "辞書ツールが開きます") {
          Button("開く") { store.run { store.actions.openTool("dictionary_tool") } }
        }
      }
      SettingsGroup(title: "用例辞書") {
        Row(title: "同音異義語辞書") {
          Switch(isOn: store.bool("information_list_config.use_local_usage_dictionary"))
        }
      }
      SettingsGroup(title: "特殊変換") {
        ForEach([("use_single_kanji_conversion", "単漢字変換"),
                 ("use_symbol_conversion", "記号変換"),
                 ("use_emoticon_conversion", "顔文字変換"),
                 ("use_emoji_conversion", "絵文字変換"),
                 ("use_date_conversion", "日付変換"),
                 ("use_number_conversion", "特殊数字変換"),
                 ("use_calculator", "計算機機能"),
                 ("use_t13n_conversion", "カタカナ英語変換"),
                 ("use_zip_code_conversion", "郵便番号変換"),
                 ("use_spelling_correction", "「もしかして」変換")], id: \.0) { item in
          Row(title: item.1) { Switch(isOn: store.bool(item.0)) }
        }
      }
    }
    .alert("学習履歴のクリア", isPresented: $confirmClear) {
      Button("消す", role: .destructive) { store.run { store.actions.clearHistory() } }
      Button("キャンセル", role: .cancel) {}
    } message: {
      Text("すべての入力履歴が削除されます。実行しますか？")
    }
  }
}

struct AssistPage: View {
  @ObservedObject var store: SettingsStore
  @State private var showLearned = false

  var body: some View {
    let auto = store.bool("use_auto_conversion").wrappedValue
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "入力補助") {
        Row(title: "自動英数変換を有効にする") {
          Switch(isOn: store.bool("auto_switch_composition_mode"))
        }
        Row(title: "句読点変換を有効にする") {
          Switch(isOn: store.bool("use_auto_conversion"))
        }
        SubRow(title: "変換する文字") {
          CharacterBoxes(items: [("。", 1), ("、", 2), ("?", 4), ("!", 8)].map {
            ($0.0, store.flag("auto_conversion_key", $0.1))
          })
        }.disabled(!auto)
        Row(title: "シフトキーでの入力切替") {
          Choice(selection: store.choice("shift_key_mode_switch"),
                 items: [("OFF", "オフ"), ("ASCII_INPUT_MODE", "英数字"),
                         ("KATAKANA_INPUT_MODE", "カタカナ")])
        }
        Row(title: "日本語入力では常に日本語キー配列を使う") {
          Switch(isOn: store.bool("use_japanese_layout"))
        }
      }
      SettingsGroup(title: "zenz のメモリと学習") {
        Row(title: "打っていない間は止める",
            detail: "zenz のメモリ 約 30MB を返す。1 分で止まり、次の 1 文字目で起きる") {
          Switch(isOn: store.bool("zenz_stop_when_idle"))
        }
        Row(title: "zenz の補正の採用と却下を覚える",
            detail: "確定した補正、退けた語、control+delete で止めた補正") {
          Switch(isOn: store.bool("zenz_learning"))
        }
        Row(title: "zenz が直した確定を予測候補に覚えさせる") {
          Switch(isOn: store.bool("zenz_suggestion_learning"))
        }
        Row(title: "zenz が直した語を、確定したら変換の順位にも覚えさせる",
            detail: "次から zenz を待たずに出る。zenz は、覚えた語を別の文で直せる") {
          Switch(isOn: store.bool("zenz_teaches_mozc"))
        }
        Row(title: "覚えた補正をすぐ出す読みの長さ", detail: "1 でいつも") {
          Amount(value: store.number("zenz_remembered_min_chars"), range: 1...20, step: 1,
                       unit: "文字以上")
        }
        Row(title: "学習データ", detail: "退けた語、すぐ出す補正、止めた補正") {
          Button("見る…") { showLearned = true }
        }
      }
    }
    .sheet(isPresented: $showLearned) { LearningDataView(actions: store.actions) }
  }
}

struct SuggestPage: View {
  @ObservedObject var store: SettingsStore

  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "サジェストの種類") {
        Row(title: "入力履歴からのサジェスト自動表示を有効にする") {
          Switch(isOn: store.bool("use_history_suggest"))
        }
        Row(title: "システム辞書からのサジェスト自動表示を有効にする") {
          Switch(isOn: store.bool("use_dictionary_suggest"))
        }
        Row(title: "リアルタイム変換を有効にする") {
          Switch(isOn: store.bool("use_realtime_conversion"))
        }
        Row(title: "足した辞書（人名、地名、SudachiDict、ニコニコ・pixiv）の語も出す",
            detail: "オフなら、読みを最後まで打って変換したときだけ出る") {
          Switch(isOn: store.bool("suggest_added_words"))
        }
      }
      SettingsGroup(title: "その他の設定") {
        Row(title: "サジェストの最大候補数") {
          Amount(value: store.number("suggestions_size"), range: 1...9, step: 1, unit: "")
        }
      }
    }
  }
}

struct PrivacyPage: View {
  @ObservedObject var store: SettingsStore

  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "シークレットモード") {
        Row(title: "一時的に学習機能、入力履歴からのサジェスト機能、ユーザ辞書機能を無効にする") {
          Switch(isOn: store.bool("incognito_mode"))
        }
      }
      SettingsGroup(title: "プレゼンテーションモード") {
        Row(title: "一時的にすべてのサジェスト機能を無効にする") {
          Switch(isOn: store.bool("presentation_mode"))
        }
        // As the Qt screen: nothing to turn off when no suggestion is on.
        .disabled(!["use_history_suggest", "use_dictionary_suggest", "use_realtime_conversion"]
          .contains { store.bool($0).wrappedValue })
      }
    }
  }
}

struct MiscPage: View {
  @ObservedObject var store: SettingsStore
  @StateObject private var updater: Updater
  @State private var startsAtLogin: Bool

  init(store: SettingsStore) {
    self.store = store
    _updater = StateObject(wrappedValue: Updater(actions: store.actions))
    _startsAtLogin = State(initialValue: store.actions.startsAtLogin())
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "更新") {
        Row(title: "Floatype の更新", detail: updater.status) {
          Button(updater.buttonTitle) { updater.press() }.disabled(updater.busy)
        }
      }
      SettingsGroup(title: "スタートアップ") {
        Row(title: "ログイン時に変換エンジンプログラムを起動する") {
          Switch(isOn: Binding(
            get: { startsAtLogin },
            set: {
              store.actions.setStartsAtLogin($0)
              startsAtLogin = store.actions.startsAtLogin()
            }))
        }
      }
    }
  }
}
