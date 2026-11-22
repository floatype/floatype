// floatype: the settings app (issue #9). It reads the converter's settings
// through the bridge and saves each change there, as the Qt screen does.

import FloatypeSettingsBridge
import FloatypeSettingsUI
import Foundation
import SwiftUI

/// Text the bridge made, freed once read; nil stays nil.
func take(_ text: UnsafeMutablePointer<CChar>?) -> String? {
  guard let text else { return nil }
  defer { FloatypeSettingsFree(text) }
  return String(cString: text)
}

let bridgeActions = SettingsActions(
  version: take(FloatypeSettingsVersion()) ?? "",
  load: { take(FloatypeSettingsLoad()) },
  reset: { take(FloatypeSettingsReset()) },
  clearHistory: { take(FloatypeSettingsClearHistory()) },
  openTool: { take(FloatypeSettingsLaunchTool($0)) },
  startsAtLogin: { FloatypeSettingsStartsAtLogin() != 0 },
  setStartsAtLogin: { FloatypeSettingsSetStartsAtLogin($0 ? 1 : 0) },
  latestReleaseURL: String(cString: FloatypeSettingsLatestReleaseUrl()),
  checkRelease: { status, tag, assets in
    let lines = assets.map { "\($0.0)\t\($0.1)\n" }.joined()
    var version: UnsafeMutablePointer<CChar>?
    var package: UnsafeMutablePointer<CChar>?
    var checksum: UnsafeMutablePointer<CChar>?
    switch FloatypeSettingsCheckRelease(Int32(status), tag, lines, &version, &package, &checksum) {
    case 0:
      guard let version = take(version), let package = take(package).flatMap(URL.init),
            let checksum = take(checksum).flatMap(URL.init) else { return .failed }
      return .newer(version: version, package: package, checksum: checksum)
    case 1: return .upToDate
    case 2: return .noRelease
    default: return .failed
    }
  },
  checksumMatches: { FloatypeSettingsChecksumMatches($0, $1) != 0 })

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
    }
    .windowStyle(.hiddenTitleBar)
  }
}
