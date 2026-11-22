// floatype: draws the settings window into PNGs, light and dark, from
// settings in a file (ConfigLines), with nothing saved and no converter
// reached (外観's preview is drawn through the bridge, which needs none): `settings_capture <lines file> <output folder> [height] [page]`.

import AppKit
import FloatypeSettingsBridgeActions
import FloatypeSettingsUI
import SwiftUI

let arguments = CommandLine.arguments
let lines = try String(contentsOfFile: arguments[1], encoding: .utf8)
let folder = URL(fileURLWithPath: arguments[2])
// An optional third argument sets the height, to see a whole page.
let size = NSSize(width: 860, height: arguments.count > 3 ? Double(arguments[3])! : 640)
let page = arguments.count > 4 ? arguments[4] : "general"

_ = NSApplication.shared
for (name, appearance) in [("light", NSAppearance.Name.aqua), ("dark", .darkAqua)] {
  var actions = SettingsActions.none()
  actions.windowLook = bridgeActions.windowLook
  actions.indicatorLabel = bridgeActions.indicatorLabel
  actions.numberForm = bridgeActions.numberForm
  // The editor windows ("learned", ...) with made-up data, nothing read.
  actions.learned = {
    [LearnedEntry(kind: 0, reading: "かんじ", text: "完治"),
     LearnedEntry(kind: 1, reading: "きかい", text: "機会"),
     LearnedEntry(kind: 2, reading: "よい", text: "良い"),
     LearnedEntry(kind: 3, reading: "ほう", text: "方")]
  }
  actions.englishWords = {
    [EnglishWord(word: "quokka", kept: true, from: 2),
     EnglishWord(word: "page", kept: false, from: 1),
     EnglishWord(word: "time", kept: true, from: 0)]
  }
  let store = SettingsStore(lines: lines, save: { _ in nil }, actions: actions)
  let root: AnyView
  switch page {
  case "learned":
    root = AnyView(LearningDataView(actions: actions).background(Color(nsColor: .windowBackgroundColor)))
  case "words":
    root = AnyView(EnglishWordsView(actions: actions).background(Color(nsColor: .windowBackgroundColor)))
  default: root = AnyView(SettingsView(store: store, page: page))
  }
  let host = NSHostingView(rootView: root)
  let window = NSWindow(
    contentRect: NSRect(origin: .zero, size: size),
    styleMask: [.titled, .fullSizeContentView], backing: .buffered, defer: false)
  window.titlebarAppearsTransparent = true
  window.titleVisibility = .hidden
  window.appearance = NSAppearance(named: appearance)
  window.contentView = host
  host.layoutSubtreeIfNeeded()
  RunLoop.main.run(until: Date().addingTimeInterval(0.5))
  guard let image = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { exit(1) }
  host.cacheDisplay(in: host.bounds, to: image)
  let file = folder.appendingPathComponent("\(name)-\(page).png")
  try image.representation(using: .png, properties: [:])!.write(to: file)
  print(file.path)
}
