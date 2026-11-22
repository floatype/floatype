// floatype: the settings screen's actions done through the bridge (issue #9),
// for the app, and for settings_capture's previews of 外観.

import FloatypeSettingsBridge
import FloatypeSettingsUI
import Foundation

/// Text the bridge made, freed once read; nil stays nil.
public func take(_ text: UnsafeMutablePointer<CChar>?) -> String? {
  guard let text else { return nil }
  defer { FloatypeSettingsFree(text) }
  return String(cString: text)
}

public let bridgeActions = SettingsActions(
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
  checksumMatches: { FloatypeSettingsChecksumMatches($0, $1) != 0 },
  windowLook: { lines, suggestion, dark in
    var look = FloatypeWindowLook()
    guard FloatypeSettingsWindowLook(lines, suggestion ? 1 : 0, dark ? 1 : 0, &look) != 0 else {
      return nil
    }
    let minimum = withUnsafeBytes(of: look.column_minimum_width) {
      String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self)
    }
    return WindowLook(
      dark: look.dark != 0, background: look.background, text: look.text,
      secondaryText: look.secondary_text, focusedBackground: look.focused_background,
      focusedText: look.focused_text, focusedSecondaryText: look.focused_secondary_text,
      focusedBorder: look.focused_border, focusedBorderAlpha: look.focused_border_alpha,
      border: look.border, borderAlpha: look.border_alpha,
      sizePercent: look.size_percent, windowPadding: look.window_padding, rowHeight: look.row_height,
      rowPadding: look.row_padding, rowCornerRadius: look.row_corner_radius,
      numberWidth: look.number_width, footerTopPadding: look.footer_top_padding,
      footerBottomPadding: look.footer_bottom_padding, cornerRadius: look.corner_radius,
      opacity: look.opacity, shadowRadius: look.shadow_radius,
      shadowOffsetY: look.shadow_offset_y, shadowOpacity: look.shadow_opacity,
      shadowColor: look.shadow_color, candidateFontSize: look.candidate_font_size,
      numberFontSize: look.number_font_size,
      descriptionFontSize: look.description_font_size,
      footerFontSize: look.footer_font_size, kindMarkColor: look.kind_mark_color,
      columnMinimumWidth: minimum)
  },
  indicatorLabel: { take(FloatypeSettingsIndicatorLabel(Int32($0), $1)) ?? "" })
