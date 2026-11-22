// floatype: the settings app (issue #9). It reads the converter's settings
// through the bridge and saves each change there, as the Qt screen does.

import FloatypeSettingsBridge
import FloatypeSettingsUI
import SwiftUI

/// The converter's settings, or nil if it cannot be reached.
func loadSettings() -> String? {
  guard let lines = FloatypeSettingsLoad() else { return nil }
  defer { FloatypeSettingsFree(lines) }
  return String(cString: lines)
}

func saveSettings(_ toml: String) -> String? {
  guard let error = FloatypeSettingsSave(toml) else { return nil }
  defer { FloatypeSettingsFree(error) }
  return String(cString: error)
}

@main
struct SettingsApp: App {
  @StateObject private var store = SettingsStore(
    lines: loadSettings() ?? "", save: saveSettings)

  var body: some Scene {
    WindowGroup {
      SettingsView(store: store)
        .frame(minWidth: 760, minHeight: 520)
    }
    .windowStyle(.hiddenTitleBar)
  }
}
