// floatype: the settings app (issue #9). It reads the converter's settings
// through the bridge and saves each change there, as the Qt screen does.

import FloatypeSettingsBridge
import FloatypeSettingsBridgeActions
import FloatypeSettingsUI
import AppKit
import Foundation
import SwiftUI

/// Quits with its window, as the Qt screen did: the IME opens the app again
/// for 環境設定…, which would otherwise only bring a windowless app forward.
final class AppDelegate: NSObject, NSApplicationDelegate {
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    true
  }
}

@main
struct SettingsApp: App {
  @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
  @StateObject private var store = SettingsStore(
    lines: bridgeActions.load(),
    save: { take(FloatypeSettingsSave($0)) },
    actions: bridgeActions)

  init() {
    FloatypeSettingsStart()
  }

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
