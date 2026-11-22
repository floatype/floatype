// floatype: the 全角・半角 table, as the Qt screen's 入力補助 tab had it: for
// each group of characters, the width while typing and once converted. Each
// change is saved at once, as the screen's other settings are.

import SwiftUI

private let forms = [("FULL_WIDTH", "全角"), ("HALF_WIDTH", "半角"), ("LAST_FORM", "学習")]

private func groupName(_ group: String) -> String {
  switch group {
  case "ア": return "カタカナ"
  case "0": return "数字"
  case "A": return "アルファベット"
  default: return group
  }
}

public struct CharacterFormsView: View {
  @ObservedObject var store: SettingsStore
  @Environment(\.dismiss) private var dismiss

  public init(store: SettingsStore) {
    self.store = store
  }

  public var body: some View {
    let table = store.characterForms()
    VStack(alignment: .leading, spacing: 10) {
      Text("半角・全角").font(.headline)
      Table(table.wrappedValue) {
        TableColumn("文字グループ") { Text(groupName($0.group)) }
        TableColumn("変換前文字列") { rule in
          FormPicker(rule: rule, table: table, typed: true)
            // Katakana is always typed full width, as in Mozc.
            .disabled(rule.group == "ア")
        }
        TableColumn("変換中文字列") { rule in FormPicker(rule: rule, table: table, typed: false) }
      }
      .frame(minHeight: 300)
      if let error = store.error {
        Text(error).foregroundColor(.red).fixedSize(horizontal: false, vertical: true)
      }
      HStack {
        Text("学習：前回変換したときの形を使う。変えた内容は、すぐ保存されます。")
          .font(.system(size: 11)).foregroundColor(.secondary)
        Spacer()
        Button("閉じる") { dismiss() }.keyboardShortcut(.cancelAction)
      }
    }
    .padding(20)
    .frame(width: 520)
  }
}

private struct FormPicker: View {
  let rule: CharacterFormRule
  let table: Binding<[CharacterFormRule]>
  let typed: Bool

  var body: some View {
    Picker(typed ? "変換前文字列" : "変換中文字列", selection: Binding(
      get: { typed ? rule.typed : rule.converted },
      set: { form in
        table.wrappedValue = table.wrappedValue.map {
          guard $0.group == rule.group else { return $0 }
          var changed = $0
          if typed { changed.typed = form } else { changed.converted = form }
          return changed
        }
      })) {
      ForEach(forms, id: \.0) { Text($0.1).tag($0.0) }
    }
    .labelsHidden()
    .fixedSize()
  }
}
