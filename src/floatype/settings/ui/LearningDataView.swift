// floatype: zenz's learning data (学習データを見る…), as the Qt screen's
// window listed it: what the user turned down, committed and stopped, to look
// through and forget.

import AppKit
import SwiftUI

private let kinds = ["退けた語", "1 回退けた語", "すぐ出す補正", "止めた補正"]

private let note = """
  退けた語：別の候補に選び直した語。zenz はこの語を出しません。自分で選んで確定すると消えます。
  1 回退けた語：補正を Space で戻して、そのまま確定した語。もう 1 回で「退けた語」になります。
  すぐ出す補正：そのまま確定した補正。次に同じ読みで、zenz を待たずに出します。
  止めた補正：control+delete で止めた補正。同じ読みでは出しません。
  消した内容は、1 秒ほどあとの変換から効きます。Mozc が覚えた候補の順は、ここには出ません。
  """

public struct LearningDataView: View {
  let actions: SettingsActions
  @Environment(\.dismiss) private var dismiss
  // The files change under the list as the converter learns, so every change
  // here reads them again.
  @State private var entries: [LearnedEntry] = []
  @State private var filter = ""
  @State private var selection = Set<LearnedEntry>()
  @State private var confirmClear = false

  public init(actions: SettingsActions) {
    self.actions = actions
    _entries = State(initialValue: actions.learned())
  }

  private var shown: [LearnedEntry] {
    filter.isEmpty ? entries
      : entries.filter { $0.reading.contains(filter) || $0.text.contains(filter) }
  }

  public var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("zenz の学習データ").font(.headline)
      TextField("読みか表記で絞り込む", text: $filter)
        .textFieldStyle(.roundedBorder)
      Table(shown, selection: $selection) {
        TableColumn("種類") { Text(kinds.indices.contains($0.kind) ? kinds[$0.kind] : "") }
          .width(min: 90, ideal: 100, max: 120)
        TableColumn("読み", value: \.reading)
        TableColumn("表記", value: \.text)
      }
      .frame(minHeight: 260)
      HStack {
        Button("選んだ行を消す") {
          selection.forEach(actions.forget)
          refresh()
        }
        .disabled(selection.isEmpty)
        Button("すべて消す…") { confirmClear = true }
          .disabled(entries.isEmpty)
        Button("Finder で表示") {
          NSWorkspace.shared.open(URL(fileURLWithPath: actions.profileDirectory()))
        }
        Spacer()
        Text("\(shown.count) 件").foregroundColor(.secondary).monospacedDigit()
        Button("閉じる") { dismiss() }.keyboardShortcut(.cancelAction)
      }
      Text(note).font(.system(size: 11)).foregroundColor(.secondary)
        .fixedSize(horizontal: false, vertical: true)
    }
    .padding(20)
    .frame(width: 600)
    .alert("zenz の学習データ", isPresented: $confirmClear) {
      Button("すべて消す", role: .destructive) {
        actions.forgetAll()
        refresh()
      }
      Button("キャンセル", role: .cancel) {}
    } message: {
      Text("zenz が覚えた内容をすべて消します。元には戻せません。")
    }
  }

  private func refresh() {
    entries = actions.learned()
    selection = selection.intersection(entries)
  }
}
