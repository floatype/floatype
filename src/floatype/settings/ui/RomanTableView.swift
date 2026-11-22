// floatype: the romaji table editor (ローマ字テーブル), as the Qt screen's
// window had it: rows to add, change and remove, a file to read in or write
// out, Mozc's table to go back to. Nothing is saved until 保存, so a table half
// edited never takes effect.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

public struct RomanTableView: View {
  @ObservedObject var store: SettingsStore
  @Environment(\.dismiss) private var dismiss
  @State private var rows: [RomanRow]
  @State private var selection = Set<UUID>()
  @State private var confirm: (() -> Void)?
  @State private var message: String?
  @State private var capitalNote = false

  public init(store: SettingsStore) {
    self.store = store
    _rows = State(initialValue: store.actions.romanTable(store.lines))
  }

  public var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("ローマ字テーブル").font(.headline)
      Table($rows, selection: $selection) {
        TableColumn("入力") { $row in TextField("", text: $row.input).accessibilityLabel("入力") }
        TableColumn("出力") { $row in TextField("", text: $row.output).accessibilityLabel("出力") }
        TableColumn("次の入力") { $row in TextField("", text: $row.next).accessibilityLabel("次の入力") }
      }
      .frame(minHeight: 320)
      HStack {
        Button("追加") {
          let row = RomanRow(input: "", output: "", next: "")
          rows.append(row)
          selection = [row.id]
        }
        Button("選んだ行を消す") {
          rows.removeAll { selection.contains($0.id) }
          selection = []
        }
        .disabled(selection.isEmpty)
        Menu("その他") {
          Button("ファイルから読み込む…") { ask { importFile() } }
          Button("ファイルに書き出す…", action: exportFile)
          Divider()
          Button("初期値に戻す…") { ask { rows = store.actions.romanRows(nil) } }
        }
        .fixedSize()
        Spacer()
        Text("\(rows.count) 行").foregroundColor(.secondary).monospacedDigit()
      }
      if let message {
        Text(message).foregroundColor(.red).fixedSize(horizontal: false, vertical: true)
      }
      HStack {
        Spacer()
        Button("キャンセル") { dismiss() }.keyboardShortcut(.cancelAction)
        Button("保存", action: save).keyboardShortcut(.defaultAction)
      }
    }
    .padding(20)
    .frame(width: 460)
    .alert("いまのローマ字テーブルを置き換えますか？", isPresented: Binding(
      get: { confirm != nil }, set: { if !$0 { confirm = nil } })) {
      Button("置き換える", role: .destructive) { confirm?(); confirm = nil }
      Button("キャンセル", role: .cancel) { confirm = nil }
    }
    .alert("ローマ字テーブル", isPresented: $capitalNote) {
      Button("OK") { dismiss() }
    } message: {
      Text("入力に大文字があります。この表では、シフトキーでの入力切替は効きません。")
    }
  }

  /// Asks before an action that replaces the rows, unless there are none.
  private func ask(_ action: @escaping () -> Void) {
    if rows.isEmpty { action() } else { confirm = action }
  }

  private func importFile() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [.plainText, .tabSeparatedText]
    guard panel.runModal() == .OK, let url = panel.url else { return }
    guard let text = try? String(contentsOf: url, encoding: .utf8) else {
      message = "\(url.lastPathComponent) を読めませんでした。"
      return
    }
    rows = store.actions.romanRows(text)
    message = nil
  }

  private func exportFile() {
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "romantable.txt"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      try store.actions.romanTableText(rows).write(to: url, atomically: true, encoding: .utf8)
      message = nil
    } catch {
      message = "\(url.lastPathComponent) に書き出せませんでした。"
    }
  }

  private func save() {
    guard let value = store.actions.romanTableValue(rows) else {
      message = "ローマ字テーブルが空です。"
      return
    }
    guard store.save("custom_roman_table", raw: value) else {
      message = store.error
      return
    }
    if rows.contains(where: { $0.input != $0.input.lowercased() }) {
      capitalNote = true
    } else {
      dismiss()
    }
  }
}
