// floatype: the settings app (issue #9). It reads the converter's settings
// through the bridge and saves each change there, as the Qt screen does.

import FloatypeSettingsBridge
import FloatypeSettingsBridgeActions
import FloatypeSettingsUI
import Foundation
import SwiftUI

@main
struct SettingsApp: App {
  @StateObject private var store = SettingsStore(
    lines: bridgeActions.load(),
    save: { take(FloatypeSettingsSave($0)) },
    actions: bridgeActions)

  var body: some Scene {
    WindowGroup {
      SettingsView(store: store)
        .frame(minWidth: 760, minHeight: 520)
        // A color or size still waiting to be saved.
        .onReceive(NotificationCenter.default.publisher(
          for: NSApplication.willTerminateNotification)) { _ in store.savePending() }
    }
    .windowStyle(.hiddenTitleBar)
  }
}
