// floatype: what the settings screen does besides changing a setting (issue
// #9), given from outside so that settings_capture and the tests reach no
// converter and no network.

import Foundation

public enum Release: Equatable {
  case newer(version: String, package: URL, checksum: URL)
  case upToDate
  case noRelease
  case failed
}

public struct SettingsActions {
  public var version: String
  /// The settings again, as lines, or nil.
  public var load: () -> String?
  /// Each returns why it failed, or nil.
  public var reset: () -> String?
  public var clearHistory: () -> String?
  public var openTool: (String) -> String?
  public var startsAtLogin: () -> Bool
  public var setStartsAtLogin: (Bool) -> Void
  public var latestReleaseURL: String
  /// The HTTP status, the tag, and the assets' names and URLs.
  public var checkRelease: (Int, String, [(String, String)]) -> Release
  /// Whether a .sha256 file's text names the hex digest.
  public var checksumMatches: (String, String) -> Bool

  public init(
    version: String, load: @escaping () -> String?, reset: @escaping () -> String?,
    clearHistory: @escaping () -> String?, openTool: @escaping (String) -> String?,
    startsAtLogin: @escaping () -> Bool, setStartsAtLogin: @escaping (Bool) -> Void,
    latestReleaseURL: String,
    checkRelease: @escaping (Int, String, [(String, String)]) -> Release,
    checksumMatches: @escaping (String, String) -> Bool
  ) {
    self.version = version
    self.load = load
    self.reset = reset
    self.clearHistory = clearHistory
    self.openTool = openTool
    self.startsAtLogin = startsAtLogin
    self.setStartsAtLogin = setStartsAtLogin
    self.latestReleaseURL = latestReleaseURL
    self.checkRelease = checkRelease
    self.checksumMatches = checksumMatches
  }

  /// Actions that do nothing, for drawing the window.
  public static func none(version: String = "0.1.1+mozc.3.35.6354") -> SettingsActions {
    SettingsActions(
      version: version, load: { nil }, reset: { nil }, clearHistory: { nil },
      openTool: { _ in nil }, startsAtLogin: { false }, setStartsAtLogin: { _ in },
      latestReleaseURL: "", checkRelease: { _, _, _ in .failed },
      checksumMatches: { _, _ in false })
  }
}
