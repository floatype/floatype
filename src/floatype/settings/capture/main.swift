// floatype: draws the settings window into PNGs, light and dark, from
// settings in a file (ConfigLines), with nothing saved and no converter
// reached: `settings_capture <lines file> <output folder> [height]`.

import AppKit
import FloatypeSettingsUI
import SwiftUI

let arguments = CommandLine.arguments
let lines = try String(contentsOfFile: arguments[1], encoding: .utf8)
let folder = URL(fileURLWithPath: arguments[2])
// An optional third argument sets the height, to see a whole page.
let size = NSSize(width: 860, height: arguments.count > 3 ? Double(arguments[3])! : 640)

_ = NSApplication.shared
for (name, appearance) in [("light", NSAppearance.Name.aqua), ("dark", .darkAqua)] {
  let store = SettingsStore(lines: lines, save: { _ in nil })
  let host = NSHostingView(rootView: SettingsView(store: store))
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
  let file = folder.appendingPathComponent("\(name).png")
  try image.representation(using: .png, properties: [:])!.write(to: file)
  print(file.path)
}
