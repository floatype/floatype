// floatype: the keymap editor (キー設定の編集), as the Qt screen's window had
// it: rows of a mode, a key and a command to add, change and remove, a preset
// or a file to start from, a file to write out. Saving makes the keymap カスタム
// with this table, in one go; nothing takes effect before 保存.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

public struct KeymapView: View {
  @ObservedObject var store: SettingsStore
  @Environment(\.dismiss) private var dismiss
  // Every row, those not listed too: they are saved back as they were.
  @State private var rows: [KeymapRow]
  @State private var selection = Set<UUID>()
  @State private var confirm: (() -> Void)?
  @State private var message: String?
  private let names: KeymapNames

  public init(store: SettingsStore) {
    self.store = store
    _rows = State(initialValue: store.actions.keymap(store.lines))
    names = store.actions.keymapNames()
  }

  private var shown: Binding<[KeymapRow]> {
    Binding(
      get: { rows.filter(\.shown) },
      set: { edited in
        let changed = Dictionary(uniqueKeysWithValues: edited.map { ($0.id, $0) })
        rows = rows.compactMap { $0.shown ? changed[$0.id] : $0 }
      })
  }

  public var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("キー設定の編集").font(.headline)
      Table(shown, selection: $selection) {
        TableColumn("モード") { $row in
          Picker("モード", selection: $row.mode) {
            ForEach(names.modes, id: \.0) { Text($0.1).tag($0.0) }
          }
          .labelsHidden()
        }
        TableColumn("入力キー") { $row in KeyField(key: $row.key) }
        TableColumn("コマンド") { $row in
          Picker("コマンド", selection: $row.command) {
            ForEach(names.commands, id: \.0) { Text($0.1).tag($0.0) }
          }
          .labelsHidden()
        }
      }
      .frame(minHeight: 340)
      HStack {
        Button("追加") {
          let row = KeymapRow(mode: "Composition", key: "", command: names.commands.first?.0 ?? "")
          rows.insert(row, at: rows.firstIndex { !$0.shown } ?? rows.endIndex)
          selection = [row.id]
        }
        Button("選んだ行を消す") {
          rows.removeAll { selection.contains($0.id) }
          selection = []
        }
        .disabled(selection.isEmpty)
        Menu("その他") {
          Menu("定義済みのキー設定を読み込む") {
            ForEach([("ATOK", "ATOK"), ("MSIME", "MS-IME"), ("KOTOERI", "ことえり")], id: \.0) { preset in
              Button(preset.1) { ask { rows = store.actions.keymapPreset(preset.0) } }
            }
          }
          Button("ファイルから読み込む…") { ask { importFile() } }
          Button("ファイルに書き出す…", action: exportFile)
        }
        .fixedSize()
        Spacer()
        Text("\(shown.wrappedValue.count) 行").foregroundColor(.secondary).monospacedDigit()
      }
      if let message {
        Text(message).foregroundColor(.red).fixedSize(horizontal: false, vertical: true)
      }
      HStack {
        Text("保存すると、キー設定の選択が「カスタム」になります。")
          .font(.system(size: 11)).foregroundColor(.secondary)
        Spacer()
        Button("キャンセル") { dismiss() }.keyboardShortcut(.cancelAction)
        // No Return shortcut: Return finishes a cell, and must not save.
        Button("保存", action: save)
      }
    }
    .padding(20)
    .frame(width: 640)
    .alert("いまのキー設定を置き換えますか？", isPresented: Binding(
      get: { confirm != nil }, set: { if !$0 { confirm = nil } })) {
      Button("置き換える", role: .destructive) { confirm?(); confirm = nil }
      Button("キャンセル", role: .cancel) { confirm = nil }
    }
  }

  private func ask(_ action: @escaping () -> Void) {
    if shown.wrappedValue.isEmpty { action() } else { confirm = action }
  }

  private func importFile() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [.plainText, .tabSeparatedText]
    guard panel.runModal() == .OK, let url = panel.url else { return }
    guard let text = try? String(contentsOf: url, encoding: .utf8) else {
      message = "\(url.lastPathComponent) を読めませんでした。"
      return
    }
    rows = store.actions.keymapRows(text)
    message = nil
  }

  private func exportFile() {
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "keymap.txt"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      try store.actions.keymapText(rows).write(to: url, atomically: true, encoding: .utf8)
      message = nil
    } catch {
      message = "\(url.lastPathComponent) に書き出せませんでした。"
    }
  }

  private func save() {
    if let bad = shown.wrappedValue.first(where: { !store.actions.keymapKeyIsValid($0.key) }) {
      message = bad.key.isEmpty ? "入力キーが空の行があります。" : "使えないキーです：\(bad.key)"
      return
    }
    guard let value = store.actions.keymapValue(rows) else {
      message = "キー設定が空です。定義済みのキー設定を読み込んでから変えてください。"
      return
    }
    if store.saveAll([("custom_keymap_table", value), ("session_keymap", "\"CUSTOM\"")]) {
      dismiss()
    } else {
      message = store.error
    }
  }
}

/// A key, set by pressing it: "Ctrl Shift a", "Henkan", as Mozc names them.
struct KeyField: View {
  @Binding var key: String
  @State private var listening = false
  @State private var monitor: Any?

  var body: some View {
    Button(listening ? "キーを押す…" : (key.isEmpty ? "（未設定）" : key)) {
      listening ? stop() : start()
    }
    .buttonStyle(.borderless)
    .foregroundColor(listening ? .accentColor : .primary)
    .onDisappear(perform: stop)
    .accessibilityLabel("入力キー \(key)")
  }

  private func start() {
    listening = true
    monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
      if let name = keyName(event) {
        key = name
        stop()
      }
      return nil  // The key goes to the field, not to the window.
    }
  }

  private func stop() {
    if let monitor { NSEvent.removeMonitor(monitor) }
    monitor = nil
    listening = false
  }
}

/// Mozc's name for a key pressed, as the Qt editor made it; nil for a key it
/// would not take (a letter without control or option, shift with a symbol).
func keyName(_ event: NSEvent) -> String? {
  let special: [UInt16: String] = [
    49: "Space", 36: "Enter", 76: "Enter", 48: "Tab", 51: "Backspace", 117: "Delete",
    53: "Escape", 123: "Left", 124: "Right", 125: "Down", 126: "Up", 115: "Home",
    119: "End", 116: "PageUp", 121: "PageDown", 114: "Insert", 102: "Eisu", 104: "Kana",
    122: "F1", 120: "F2", 99: "F3", 118: "F4", 96: "F5", 97: "F6", 98: "F7", 100: "F8",
    101: "F9", 109: "F10", 103: "F11", 111: "F12", 105: "F13", 107: "F14", 113: "F15",
    106: "F16", 64: "F17", 79: "F18", 80: "F19", 90: "F20",
  ]
  let flags = event.modifierFlags
  let ctrl = flags.contains(.control), shift = flags.contains(.shift)
  let alt = flags.contains(.option)
  var parts: [String] = []
  if ctrl { parts.append("Ctrl") }
  if shift { parts.append("Shift") }
  if alt { parts.append("Option") }
  if let name = special[event.keyCode] {
    return (parts + [name]).joined(separator: " ")
  }
  // A printable key needs control or option; with shift, only a letter.
  let base = event.keyCode == 93 ? "\\" : (event.charactersIgnoringModifiers ?? "")
  guard base.count == 1, let char = base.lowercased().unicodeScalars.first,
        char.value >= 0x21, char.value <= 0x7e, ctrl || alt else { return nil }
  let letter = char.properties.isAlphabetic
  if shift && !letter { return nil }
  return (parts + [String(char)]).joined(separator: " ")
}
