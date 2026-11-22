// floatype: the settings window (issue #9): pages listed on the left with
// icons, the page's name large, its settings in rounded groups, each with a
// gray line saying what it does and its control at the right edge. Built
// from parts macOS 12 has.

import AppKit
import SwiftUI

struct Page: Identifiable, Hashable {
  let id: String
  let title: String
  let symbol: String
  let color: Color
}

let pages = [
  Page(id: "general", title: "一般", symbol: "gearshape.fill", color: .blue),
  Page(id: "dictionary", title: "辞書", symbol: "book.fill", color: .orange),
  Page(id: "assist", title: "入力補助", symbol: "keyboard", color: .green),
  Page(id: "suggest", title: "サジェスト", symbol: "text.bubble.fill", color: .purple),
  Page(id: "look", title: "外観", symbol: "paintbrush.fill", color: .pink),
  Page(id: "privacy", title: "プライバシー", symbol: "hand.raised.fill", color: .teal),
  Page(id: "misc", title: "その他", symbol: "ellipsis.circle.fill", color: .gray),
]

public struct SettingsView: View {
  @ObservedObject var store: SettingsStore
  @State private var page: Page
  private let lookTarget: String

  /// `page` names the page shown first ("general", "dictionary", ...; for
  /// 外観, "look" or "look.ruby" with what it shows first).
  public init(store: SettingsStore, page: String = "general") {
    self.store = store
    let parts = page.split(separator: ".", maxSplits: 1).map(String.init)
    _page = State(initialValue: pages.first { $0.id == parts.first } ?? pages[0])
    lookTarget = parts.count > 1 ? parts[1] : "candidate"
  }

  public var body: some View {
    HStack(spacing: 0) {
      Sidebar(page: $page)
        .frame(width: 200)
      Divider()
      VStack(alignment: .leading, spacing: 0) {
        Text(page.title)
          .font(.system(size: 26, weight: .bold))
          .padding(.horizontal, 28)
          .padding(.top, 30)
          .padding(.bottom, 12)
        ScrollView {
          Group {
            if !store.loaded {
              Text("変換エンジンにつながらないので、設定を読めませんでした。")
                .foregroundColor(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
            } else {
              switch page.id {
              case "general": GeneralPage(store: store)
              case "dictionary": DictionaryPage(store: store)
              case "assist": AssistPage(store: store)
              case "suggest": SuggestPage(store: store)
              case "look": LookPage(store: store, target: lookTarget)
              case "privacy": PrivacyPage(store: store)
              default: MiscPage(store: store)
              }
            }
          }
          .padding(.horizontal, 28)
          .padding(.bottom, 24)
        }
        if let error = store.error {
          Text(error)
            .font(.callout)
            .foregroundColor(.red)
            .padding(.horizontal, 28)
            .padding(.bottom, 12)
        }
        Footer(store: store)
      }
      .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
      .background(Color(nsColor: .windowBackgroundColor))
    }
    // Up to the window's top edge, where the title bar would be.
    .ignoresSafeArea()
  }
}

struct Sidebar: View {
  @Binding var page: Page

  var body: some View {
    VStack(alignment: .leading, spacing: 2) {
      ForEach(pages) { item in
        Button {
          page = item
        } label: {
          HStack(spacing: 9) {
            Image(systemName: item.symbol)
              .font(.system(size: 11, weight: .semibold))
              .foregroundColor(.white)
              .frame(width: 22, height: 22)
              .background(RoundedRectangle(cornerRadius: 6).fill(item.color))
            Text(item.title)
              .foregroundColor(item == page ? .white : .primary)
            Spacer()
          }
          .padding(.horizontal, 8)
          .padding(.vertical, 5)
          .background(
            RoundedRectangle(cornerRadius: 7)
              .fill(item == page ? Color.accentColor : Color.clear))
          .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
      }
      Spacer()
    }
    .padding(.horizontal, 10)
    // Below the window's buttons, which sit over the list.
    .padding(.top, 52)
    .frame(maxHeight: .infinity)
    .background(Color(nsColor: .underPageBackgroundColor).opacity(0.5))
  }
}

/// Settings under a small title, in a rounded box.
struct SettingsGroup<Content: View>: View {
  let title: String
  @ViewBuilder let content: Content

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      Text(title)
        .font(.system(size: 12, weight: .semibold))
        .foregroundColor(.secondary)
        .padding(.leading, 4)
      VStack(spacing: 0) { content }
      .background(
        RoundedRectangle(cornerRadius: 10)
          .fill(Color.primary.opacity(0.04)))
      .overlay(
        RoundedRectangle(cornerRadius: 10)
          .stroke(Color.primary.opacity(0.08)))
    }
    .padding(.bottom, 18)
  }
}

/// A setting: its name and what it does on the left, its control on the right.
struct Row<Control: View>: View {
  let title: String
  var detail: String? = nil
  @ViewBuilder let control: Control
  @Environment(\.isEnabled) private var isEnabled

  var body: some View {
    HStack(alignment: .center, spacing: 16) {
      VStack(alignment: .leading, spacing: 2) {
        Text(title)
          .foregroundColor(isEnabled ? .primary : .secondary)
        if let detail {
          Text(detail)
            .font(.system(size: 11))
            .foregroundColor(.secondary)
            .fixedSize(horizontal: false, vertical: true)
        }
      }
      Spacer(minLength: 8)
      control.environment(\.rowTitle, title)
    }
    .padding(.horizontal, 14)
    .padding(.vertical, 9)
  }
}

private struct RowTitleKey: EnvironmentKey {
  static let defaultValue = ""
}

extension EnvironmentValues {
  /// The name of the row a control sits in, which VoiceOver reads as the
  /// control's label, the label shown being the row's.
  var rowTitle: String {
    get { self[RowTitleKey.self] }
    set { self[RowTitleKey.self] = newValue }
  }
}

struct Choice: View {
  let selection: Binding<String>
  let items: [(String, String)]  // (value name, shown)
  @Environment(\.rowTitle) private var title

  var body: some View {
    Picker(title, selection: selection) {
      ForEach(items, id: \.0) { Text($0.1).tag($0.0) }
    }
    .labelsHidden()
    .pickerStyle(.menu)
    .fixedSize()
  }
}

struct Switch: View {
  let isOn: Binding<Bool>
  @Environment(\.rowTitle) private var title

  var body: some View {
    Toggle(title, isOn: isOn).labelsHidden().toggleStyle(.switch)
  }
}

/// A number with its unit, and a stepper.
struct Amount: View {
  let value: Binding<Int>
  let range: ClosedRange<Int>
  let step: Int
  var unit = "ms"
  @Environment(\.rowTitle) private var title

  var body: some View {
    HStack(spacing: 6) {
      Text(unit.isEmpty ? "\(value.wrappedValue)" : "\(value.wrappedValue) \(unit)")
        .monospacedDigit()
      Stepper(title, value: value, in: range, step: step).labelsHidden()
    }
  }
}

struct GeneralPage: View {
  @ObservedObject var store: SettingsStore
  @State private var showRoman = false

  var body: some View {
    let live = store.bool("use_live_conversion").wrappedValue
    let zenz = store.bool("use_zenz").wrappedValue
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "基本設定") {
        Row(title: "ローマ字入力・かな入力") {
          Choice(selection: store.choice("preedit_method"),
                 items: [("ROMAN", "ローマ字入力"), ("KANA", "かな入力")])
        }
        SubRow(title: "ローマ字テーブル") {
          Button("編集…") { showRoman = true }
        }.disabled(store.choice("preedit_method").wrappedValue != "ROMAN")
        Row(title: "句読点") {
          Choice(selection: store.choice("punctuation_method"),
                 items: [("TOUTEN_KUTEN", "、。"), ("COMMA_PERIOD", "，．"),
                         ("TOUTEN_PERIOD", "、．"), ("COMMA_KUTEN", "，。")])
        }
        Row(title: "記号") {
          Choice(selection: store.choice("symbol_method"),
                 items: [("CORNER_BRACKET_MIDDLE_DOT", "「」・"),
                         ("SQUARE_BRACKET_SLASH", "[]／"),
                         ("CORNER_BRACKET_SLASH", "「」／"),
                         ("SQUARE_BRACKET_MIDDLE_DOT", "[]・")])
        }
        Row(title: "¥キーで入力する文字") {
          Choice(selection: store.choice("yen_sign_character"),
                 items: [("YEN_SIGN", "円記号（¥）"), ("BACKSLASH", "バックスラッシュ（\\）")])
        }
        Row(title: "スペースの入力") {
          Choice(selection: store.choice("space_character_form"),
                 items: [("FUNDAMENTAL_INPUT_MODE", "入力モードに従う"),
                         ("FUNDAMENTAL_FULL_WIDTH", "全角"),
                         ("FUNDAMENTAL_HALF_WIDTH", "半角")])
        }
        Row(title: "候補選択ショートカット") {
          Choice(selection: store.choice("selection_shortcut"),
                 items: [("NO_SHORTCUT", "なし"), ("SHORTCUT_123456789", "1 -- 9"),
                         ("SHORTCUT_ASDFGHJKL", "A -- L")])
        }
        Row(title: "テンキーからの入力") {
          Choice(selection: store.choice("numpad_character_form"),
                 items: [("NUMPAD_INPUT_MODE", "入力モードに従う"),
                         ("NUMPAD_FULL_WIDTH", "全角"), ("NUMPAD_HALF_WIDTH", "半角"),
                         ("NUMPAD_DIRECT_INPUT", "直接入力")])
        }
        Row(title: "数字の入力") {
          let form = store.numberForm()
          // The 4th only shows the table's own pair; choosing it sets nothing.
          Choice(selection: form,
                 items: [("0", "半角"), ("1", "全角"),
                         ("2", "全角（変換は前回の形）")]
                   + (form.wrappedValue == "3" ? [("3", "表で個別に設定")] : []))
        }
      }
      SettingsGroup(title: "キー設定") {
        Row(title: "キー設定の選択") {
          let keymap = store.choice("session_keymap")
          // カスタム only with a table to use: an empty one is refused.
          let custom = keymap.wrappedValue == "CUSTOM"
            || (store.values["custom_keymap_table"] ?? "\"\"") != "\"\""
          Choice(selection: keymap,
                 items: (custom ? [("CUSTOM", "カスタム")] : [])
                   + [("ATOK", "ATOK"), ("MSIME", "MS-IME"), ("KOTOERI", "ことえり")])
        }
      }
      SettingsGroup(title: "ライブ変換") {
        Row(title: "手が止まったら Space を押さずに変換する") {
          Switch(isOn: store.bool("use_live_conversion"))
        }
        Row(title: "変換するまでの待ち時間") {
          Amount(value: store.number("live_conversion_delay_millisec"),
                       range: 0...2000, step: 10)
        }.disabled(!live)
        Row(title: "変換を始める文字数") {
          Amount(value: store.number("live_conversion_min_chars"),
                       range: 1...10, step: 1, unit: "文字")
        }.disabled(!live)
      }
      SettingsGroup(title: "zenz の補正") {
        Row(title: "zenz で変換を補正する") {
          Switch(isOn: store.bool("use_zenz"))
        }
        Row(title: "zenz に頼むまでの待ち時間") {
          Amount(value: store.number("zenz_delay_millisec"), range: 0...5000, step: 10)
        }.disabled(!zenz)
        Row(title: "答えを待つ上限") {
          Amount(value: store.number("zenz_timeout_millisec"), range: 50...5000, step: 50)
        }.disabled(!zenz)
        Row(title: "ライブ変換で zenz の答えを待つ上限", detail: "0 で待たない") {
          Amount(value: store.number("live_conversion_waits_for_zenz_millisec"),
                       range: 0...1000, step: 10)
        }.disabled(!zenz)
        Row(title: "直前に確定した文を zenz に見せる",
            detail: "前の文を教えないアプリ（ターミナルなど）で") {
          Switch(isOn: store.bool("zenz_context_from_commits"))
        }.disabled(!zenz)
      }
      InputAssistGroup(store: store)
    }
    .sheet(isPresented: $showRoman) { RomanTableView(store: store) }
  }
}

/// Under every page: where the settings live, and 初期値に戻す.
struct Footer: View {
  @ObservedObject var store: SettingsStore
  @State private var confirmReset = false

  var body: some View {
    HStack {
      Button("初期値に戻す…") { confirmReset = true }
        .disabled(!store.loaded)
      Spacer()
      Text("保存先 ~/.config/floatype/config.toml")
        .font(.system(size: 11))
        .foregroundColor(.secondary)
      Button("Finder で表示") {
        let folder = FileManager.default.homeDirectoryForCurrentUser
          .appendingPathComponent(".config/floatype")
        NSWorkspace.shared.open(folder)
      }
    }
    .padding(.horizontal, 28)
    .padding(.vertical, 12)
    .alert("初期値に戻す", isPresented: $confirmReset) {
      Button("戻す", role: .destructive) { store.reset() }
      Button("キャンセル", role: .cancel) {}
    } message: {
      Text("この機能を実行すると Floatype の設定が標準状態に戻ります。実行しますか？ "
           + "以下の設定はリセットされません。\n - 学習履歴\n - 入力履歴")
    }
  }
}
