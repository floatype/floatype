// floatype: the English words kept in letters while typing kana (英単語を編集…),
// as the Qt screen's window listed them: english_words.txt's lines and the
// preset words, to add to, take out of, and remove from the file.

import AppKit
import SwiftUI

private let note = """
  英字にする：かなの入力中に打っても英字のまま残す語。英字にしない：英字にする語から外す語。
  初めから：そのまま打つと かな としても読める語のうち、日本語では打たない語（page、time）。英字にしないに回せます。初めから → 自分で：その語をファイルで変えたもの。一般の英単語（約 8 万 8 千語）は、ここには出ません。
  変えた内容は english_words.txt に書かれ、1 秒ほどあとのキーから効きます。
  """

private let sources = ["初めから", "初めから → 自分で", "自分で"]

public struct EnglishWordsView: View {
  let actions: SettingsActions
  @Environment(\.dismiss) private var dismiss
  @State private var words: [EnglishWord] = []
  @State private var typed = ""
  @State private var keep = true
  @State private var filter = ""
  @State private var selection = Set<String>()
  @State private var message: String?

  public init(actions: SettingsActions) {
    self.actions = actions
    _words = State(initialValue: actions.englishWords())
  }

  private var shown: [EnglishWord] {
    filter.isEmpty ? words : words.filter { $0.word.localizedCaseInsensitiveContains(filter) }
  }

  private var selected: [EnglishWord] { words.filter { selection.contains($0.id) } }

  public var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("英字のまま残す英単語").font(.headline)
      HStack {
        TextField("足す語（figma）", text: $typed)
          .textFieldStyle(.roundedBorder)
          .onSubmit(add)
          .onChange(of: typed) { _ in message = nil }
        Picker("扱い", selection: $keep) {
          Text("英字にする").tag(true)
          Text("英字にしない").tag(false)
        }
        .labelsHidden()
        .fixedSize()
        Button("足す", action: add)
          .disabled(typed.trimmingCharacters(in: .whitespaces).isEmpty)
      }
      if let message {
        Text(message).foregroundColor(.red).fixedSize(horizontal: false, vertical: true)
      }
      TextField("語で絞り込む", text: $filter).textFieldStyle(.roundedBorder)
      Table(shown, selection: $selection) {
        TableColumn("語", value: \.word)
        TableColumn("扱い") { Text($0.kept ? "英字にする" : "英字にしない") }
          .width(min: 90, ideal: 100, max: 110)
        TableColumn("どこから") { Text(sources.indices.contains($0.from) ? sources[$0.from] : "") }
          .width(min: 100, ideal: 120, max: 140)
      }
      .frame(minHeight: 260)
      HStack {
        Button("選んだ語を英字にしない") {
          edit(selected.filter(\.kept).map(\.word), 1)
        }
        .disabled(!selected.contains(where: \.kept))
        Button("選んだ行をファイルから消す") {
          edit(selected.filter { $0.from != 0 }.map(\.word), 2)
        }
        .disabled(!selected.contains { $0.from != 0 })
        Button("Finder で表示") {
          NSWorkspace.shared.open(URL(fileURLWithPath: actions.userFilesDirectory()))
        }
        Spacer()
        Text("\(shown.count) 語").foregroundColor(.secondary).monospacedDigit()
        Button("閉じる") { dismiss() }.keyboardShortcut(.cancelAction)
      }
      Text(note).font(.system(size: 11)).foregroundColor(.secondary)
        .fixedSize(horizontal: false, vertical: true)
    }
    .padding(20)
    .frame(width: 660)
  }

  private func add() {
    let word = typed.trimmingCharacters(in: .whitespaces)
    guard !word.isEmpty else { return }
    if edit([word], keep ? 0 : 1) {
      typed = ""
    }
  }

  /// Writes the edit, or shows why it was not written; the list is read
  /// again either way (the file may have changed under it).
  @discardableResult
  private func edit(_ list: [String], _ how: Int) -> Bool {
    guard !list.isEmpty else { return false }
    message = actions.editEnglishWords(list, how)
    words = actions.englishWords()
    selection = selection.intersection(words.map(\.id))
    return message == nil
  }
}
