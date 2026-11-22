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

/// A candidate window's look as the renderer draws it, for 外観's preview:
/// colors 0xRRGGBB, measures and font sizes in points.
public struct WindowLook {
  public var dark: Bool
  public var background, text, secondaryText: UInt32
  public var focusedBackground, focusedText, focusedSecondaryText: UInt32
  public var focusedBorder: UInt32
  public var focusedBorderAlpha: Double
  public var border: UInt32
  public var borderAlpha: Double
  public var sizePercent: Double
  public var windowPadding, rowHeight, rowPadding, rowCornerRadius: Double
  public var numberWidth, footerTopPadding, footerBottomPadding: Double
  public var cornerRadius, opacity: Double
  public var shadowRadius, shadowOffsetY, shadowOpacity: Double
  public var shadowColor: UInt32
  public var candidateFontSize, numberFontSize, descriptionFontSize: Double
  public var footerFontSize: Double
  public var kindMarkColor: UInt32
  public var columnMinimumWidth: String

  public init(
    dark: Bool, background: UInt32, text: UInt32, secondaryText: UInt32,
    focusedBackground: UInt32, focusedText: UInt32, focusedSecondaryText: UInt32,
    focusedBorder: UInt32, focusedBorderAlpha: Double, border: UInt32, borderAlpha: Double,
    sizePercent: Double, windowPadding: Double, rowHeight: Double, rowPadding: Double, rowCornerRadius: Double,
    numberWidth: Double, footerTopPadding: Double, footerBottomPadding: Double,
    cornerRadius: Double, opacity: Double, shadowRadius: Double, shadowOffsetY: Double,
    shadowOpacity: Double, shadowColor: UInt32, candidateFontSize: Double,
    numberFontSize: Double, descriptionFontSize: Double, footerFontSize: Double,
    kindMarkColor: UInt32, columnMinimumWidth: String
  ) {
    self.dark = dark
    self.background = background
    self.text = text
    self.secondaryText = secondaryText
    self.focusedBackground = focusedBackground
    self.focusedText = focusedText
    self.focusedSecondaryText = focusedSecondaryText
    self.focusedBorder = focusedBorder
    self.focusedBorderAlpha = focusedBorderAlpha
    self.border = border
    self.borderAlpha = borderAlpha
    self.sizePercent = sizePercent
    self.windowPadding = windowPadding
    self.rowHeight = rowHeight
    self.rowPadding = rowPadding
    self.rowCornerRadius = rowCornerRadius
    self.numberWidth = numberWidth
    self.footerTopPadding = footerTopPadding
    self.footerBottomPadding = footerBottomPadding
    self.cornerRadius = cornerRadius
    self.opacity = opacity
    self.shadowRadius = shadowRadius
    self.shadowOffsetY = shadowOffsetY
    self.shadowOpacity = shadowOpacity
    self.shadowColor = shadowColor
    self.candidateFontSize = candidateFontSize
    self.numberFontSize = numberFontSize
    self.descriptionFontSize = descriptionFontSize
    self.footerFontSize = footerFontSize
    self.kindMarkColor = kindMarkColor
    self.columnMinimumWidth = columnMinimumWidth
  }
}

/// One line of zenz's learning data; `kind` 0 退けた語, 1 1 回退けた語,
/// 2 すぐ出す補正, 3 止めた補正.
public struct LearnedEntry: Hashable, Identifiable {
  public var kind: Int
  public var reading: String
  public var text: String
  public var id: Self { self }

  public init(kind: Int, reading: String, text: String) {
    self.kind = kind
    self.reading = reading
    self.text = text
  }
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
  /// The look with the settings lines given, for the prediction list or not,
  /// in a dark system or not; nil if the lines cannot be read.
  public var windowLook: (String, Bool, Bool) -> WindowLook?
  /// What the indicator shows for a state (1 入力中, 2 Mozc, 3 zenz pending,
  /// 4 Zenz) with a label setting ("FULL_LABEL", ...).
  public var indicatorLabel: (Int, String) -> String
  /// 一般's 数字の入力 (0 半角, 1 全角, 2 Mozc's default, 3 another pair)
  /// with the settings lines given.
  public var numberForm: (String) -> Int
  /// character_form_rules's value with the digits' rule set to a 数字の入力
  /// (0 to 2), or nil without that rule.
  public var withNumberForm: (String, Int) -> String?
  /// zenz's learning data, by kind, the newest of each first.
  public var learned: () -> [LearnedEntry]
  public var forget: (LearnedEntry) -> Void
  public var forgetAll: () -> Void
  /// The folder the learning data is in.
  public var profileDirectory: () -> String

  public init(
    version: String, load: @escaping () -> String?, reset: @escaping () -> String?,
    clearHistory: @escaping () -> String?, openTool: @escaping (String) -> String?,
    startsAtLogin: @escaping () -> Bool, setStartsAtLogin: @escaping (Bool) -> Void,
    latestReleaseURL: String,
    checkRelease: @escaping (Int, String, [(String, String)]) -> Release,
    checksumMatches: @escaping (String, String) -> Bool,
    windowLook: @escaping (String, Bool, Bool) -> WindowLook? = { _, _, _ in nil },
    indicatorLabel: @escaping (Int, String) -> String = { _, _ in "" },
    numberForm: @escaping (String) -> Int = { _ in 3 },
    withNumberForm: @escaping (String, Int) -> String? = { _, _ in nil },
    learned: @escaping () -> [LearnedEntry] = { [] },
    forget: @escaping (LearnedEntry) -> Void = { _ in },
    forgetAll: @escaping () -> Void = {},
    profileDirectory: @escaping () -> String = { "" }
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
    self.windowLook = windowLook
    self.indicatorLabel = indicatorLabel
    self.numberForm = numberForm
    self.withNumberForm = withNumberForm
    self.learned = learned
    self.forget = forget
    self.forgetAll = forgetAll
    self.profileDirectory = profileDirectory
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
