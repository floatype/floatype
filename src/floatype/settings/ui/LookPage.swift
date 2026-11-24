// floatype: 外観 (issue #9): what the candidate window, the prediction list,
// the ruby and the indicator look like, with a preview drawn as the Qt screen's
// look_preview.cc draws it. Colors and measures come from the renderer's code
// through the bridge (WindowLook), not from here.

import AppKit
import SwiftUI

struct LookPage: View {
  @ObservedObject var store: SettingsStore
  @State private var target: String
  /// The preview's backdrop, and what the automatic theme follows; nil
  /// follows the system.
  @State private var dark: Bool?
  @Environment(\.colorScheme) private var scheme

  init(store: SettingsStore, target: String = "candidate") {
    self.store = store
    _target = State(initialValue: target)
  }

  var body: some View {
    let shownDark = dark ?? (scheme == .dark)
    VStack(alignment: .leading, spacing: 0) {
      Picker("見る所", selection: $target) {
        Text("候補の窓").tag("candidate")
        Text("予測候補の窓").tag("suggest")
        Text("ルビ").tag("ruby")
        Text("インジケータ").tag("indicator")
      }
      .labelsHidden()
      .pickerStyle(.segmented)
      .padding(.bottom, 14)
      SettingsGroup(title: "見本") {
        VStack(spacing: 10) {
          LookPreview(store: store, target: target, dark: shownDark)
            .frame(height: 300)
            .accessibilityHidden(true)
          Picker("見本の明るさ", selection: Binding(get: { shownDark }, set: { dark = $0 })) {
            Text("明").tag(false)
            Text("暗").tag(true)
          }
          .labelsHidden()
          .pickerStyle(.segmented)
          .fixedSize()
          .help("見本の背景と、自動の配色が従う明るさ")
        }
        .padding(12)
      }
      switch target {
      case "candidate":
        WindowForm(store: store, prefix: "candidate_style", suggestion: false, dark: shownDark)
      case "suggest":
        WindowForm(store: store, prefix: "suggest_style", suggestion: true, dark: shownDark)
      case "ruby": RubyForm(store: store)
      default: IndicatorForm(store: store)
      }
    }
  }
}

/// A color well; the color panel saves once it stops changing.
struct ColorWell: View {
  let color: Binding<CGColor>
  @Environment(\.rowTitle) private var title

  var body: some View {
    let rgb = rgbOf(color.wrappedValue)
    HStack(spacing: 8) {
      Text(String(format: "#%06X", rgb)).monospacedDigit().foregroundColor(.secondary)
      ColorPicker(title, selection: color, supportsOpacity: false).labelsHidden()
    }
  }
}

func rgbOf(_ color: CGColor) -> UInt32 {
  guard let srgb = color.converted(
    to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
    let parts = srgb.components, parts.count >= 3 else { return 0 }
  let byte = { (part: CGFloat) in UInt32((min(max(part, 0), 1) * 255).rounded()) }
  return byte(parts[0]) << 16 | byte(parts[1]) << 8 | byte(parts[2])
}

struct WindowForm: View {
  @ObservedObject var store: SettingsStore
  let prefix: String
  let suggestion: Bool
  let dark: Bool

  var body: some View {
    let custom = store.choice("\(prefix).theme").wrappedValue == "CUSTOM"
    let look = store.actions.windowLook(store.lines, suggestion, dark)
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "配色") {
        Row(title: "配色") {
          Choice(selection: store.choice("\(prefix).theme"),
                 items: [("AUTO", "自動（システムに合わせる）"), ("LIGHT", "明るい"),
                         ("DARK", "暗い"), ("CUSTOM", "指定する")])
        }
        // Unset, a color shows the one the window gets.
        if custom, let look {
          ForEach([("background_color", "背景", look.background),
                   ("text_color", "文字", look.text),
                   ("focused_background_color", "選択中の背景", look.focusedBackground),
                   ("focused_border_color", "選択中の枠", look.focusedBorder),
                   ("border_color", "枠線", look.border)], id: \.0) { field, label, shown in
            Row(title: label) {
              ColorWell(color: store.color("\(prefix).\(field)", shown: shown))
            }
          }
        }
      }
      SettingsGroup(title: "形と文字") {
        Row(title: "文字の大きさ") {
          let size = store.numberSoon("\(prefix).size_percent")
          HStack(spacing: 8) {
            Slider(value: size, in: 50...300, step: 5).frame(width: 160)
              .accessibilityLabel("文字の大きさ")
            Text("\(Int(size.wrappedValue))%").monospacedDigit().frame(width: 44, alignment: .trailing)
          }
        }
        Row(title: "角の丸み") {
          Amount(value: store.number("\(prefix).corner_radius"), range: 0...24, step: 1, unit: "px")
        }
        Row(title: "不透明度") {
          Amount(value: store.number("\(prefix).opacity_percent"), range: 30...100, step: 5,
                 unit: "%")
        }
        Row(title: "影") {
          Choice(selection: store.choice("\(prefix).shadow"),
                 items: [("NONE", "なし"), ("SOFT", "弱い"), ("STRONG", "強い")])
        }
        Row(title: "フォント") {
          FontChoice(selection: store.text("\(prefix).font_family"))
        }
        if !suggestion {
          Row(title: "予測候補の一覧と変換候補を、帯の左の丸の色で見分ける",
              detail: "予測候補の一覧は青、変換候補は灰色の丸。色は下で変えられる") {
            Switch(isOn: store.bool("candidate_kind_mark"))
          }
        }
        // The dot's color in this window, where the dot is drawn.
        if store.bool("candidate_kind_mark").wrappedValue, let look {
          Row(title: suggestion ? "予測候補の一覧の丸の色" : "変換候補の丸の色") {
            ColorWell(color: store.color("\(prefix).kind_mark_color", shown: look.kindMarkColor))
          }
        }
      }
    }
  }
}

/// The installed font families, after the system's; one set but not
/// installed stays as it is.
struct FontChoice: View {
  let selection: Binding<String>
  @Environment(\.rowTitle) private var title

  var body: some View {
    let families = NSFontManager.shared.availableFontFamilies
    let shown = families.contains(selection.wrappedValue) || selection.wrappedValue.isEmpty
      ? families : [selection.wrappedValue] + families
    Picker(title, selection: selection) {
      Text("システム（標準）").tag("")
      ForEach(shown, id: \.self) { Text($0).tag($0) }
    }
    .labelsHidden()
    .pickerStyle(.menu)
    .frame(maxWidth: 220)
  }
}

struct RubyForm: View {
  @ObservedObject var store: SettingsStore

  var body: some View {
    SettingsGroup(title: "ルビ") {
      Row(title: "ルビを出す", detail: "ライブ変換の上に読みを小さく") {
        Switch(isOn: store.bool("live_conversion_ruby"))
      }
      Row(title: "文字の大きさ") {
        Amount(value: store.number("ruby_style.font_size"), range: 6...72, step: 1, unit: "pt")
      }
      Row(title: "文字色") {
        ColorWell(color: store.color("ruby_style.text_color", shown: 0))
      }
      Row(title: "背景色") {
        ColorWell(color: store.color("ruby_style.background_color", shown: 0))
      }
      Row(title: "角の丸み") {
        Amount(value: store.number("ruby_style.corner_radius"), range: 0...24, step: 1, unit: "px")
      }
      Row(title: "不透明度") {
        Amount(value: store.number("ruby_style.opacity_percent"), range: 30...100, step: 5,
               unit: "%")
      }
    }
  }
}

struct IndicatorForm: View {
  @ObservedObject var store: SettingsStore

  var body: some View {
    let on = store.bool("show_indicator").wrappedValue
    VStack(alignment: .leading, spacing: 0) {
      SettingsGroup(title: "インジケータ") {
        Row(title: "入力中の文字の近くに出す") {
          Switch(isOn: store.bool("show_indicator"))
        }
        Group {
          Row(title: "形") {
            Choice(selection: store.choice("indicator_style.shape"),
                   items: [("PILL", "角丸の帯"), ("ROUNDED_RECT", "角丸の四角"), ("CIRCLE", "丸")])
          }
          Row(title: "文字") {
            Choice(selection: store.choice("indicator_style.label"),
                   items: [("FULL_LABEL", "入力中 / Mozc / Zenz"), ("SYMBOL", "あ / M / Z"),
                           ("COLOR_ONLY", "色だけ")])
          }
          Row(title: "位置") {
            Choice(selection: store.choice("indicator_style.placement"),
                   items: [("ABOVE", "入力中の文字の上"), ("BELOW", "下"), ("SIDE", "左横")])
          }
        }.disabled(!on)
      }
      SettingsGroup(title: "色") {
        ForEach([("reading_color", "入力中の色"), ("mozc_color", "Mozc の色"),
                 ("zenz_pending_color", "zenz を待つ間の色"), ("zenz_color", "Zenz の色"),
                 ("text_color", "文字色")], id: \.0) { field, label in
          Row(title: label) {
            ColorWell(color: store.color("indicator_style.\(field)", shown: 0))
          }
        }
      }.disabled(!on)
      SettingsGroup(title: "大きさ") {
        Row(title: "高さ") {
          Amount(value: store.number("indicator_style.height"), range: 8...200, step: 1, unit: "px")
        }
        Row(title: "文字の大きさ") {
          Amount(value: store.number("indicator_style.font_size"), range: 6...72, step: 1,
                 unit: "pt")
        }
        Row(title: "角の丸み", detail: "角丸の四角のとき") {
          Amount(value: store.number("indicator_style.corner_radius"), range: 0...100, step: 1,
                 unit: "px")
        }
        Row(title: "不透明度") {
          Amount(value: store.number("indicator_style.opacity_percent"), range: 0...100, step: 5,
                 unit: "%")
        }
      }.disabled(!on)
    }
  }
}

// MARK: - The preview, as look_preview.cc paints it.

private func paint(_ rgb: UInt32, _ alpha: Double = 1) -> Color {
  Color(.sRGB, red: Double((rgb >> 16) & 0xff) / 255, green: Double((rgb >> 8) & 0xff) / 255,
        blue: Double(rgb & 0xff) / 255, opacity: alpha)
}

struct LookPreview: View {
  @ObservedObject var store: SettingsStore
  let target: String
  let dark: Bool

  var body: some View {
    let lines = store.lines
    let look = target == "candidate" || target == "suggest"
      ? store.actions.windowLook(lines, target == "suggest", dark) : nil
    let family = store.text("\(target)_style.font_family").wrappedValue
    let values = store.values
    Canvas { context, size in
      let area = CGRect(origin: .zero, size: size)
      let backdrop = Path(roundedRect: area.insetBy(dx: 0.5, dy: 0.5), cornerRadius: 8)
      context.fill(backdrop, with: .color(dark ? paint(0x1e1e20) : paint(0xf5f5f7)))
      context.stroke(backdrop, with: .color(dark ? paint(0x48484a) : paint(0xd0d0d4)))
      switch target {
      case "candidate", "suggest":
        if let look {
          paintWindow(&context, area, look, family: family, suggestion: target == "suggest",
                      kindMark: values["candidate_kind_mark"] == "true")
        }
      case "ruby":
        paintRuby(&context, area, values, dark: dark)
      default:
        paintIndicators(&context, area, values) { store.actions.indicatorLabel($0, $1) }
      }
    }
  }
}

private func previewFont(_ size: Double, _ family: String = "", weight: SwiftUI.Font.Weight = .regular)
  -> SwiftUI.Font
{
  family.isEmpty || NSFont(name: family, size: size) == nil && !NSFontManager.shared
    .availableFontFamilies.contains(family)
    ? .system(size: max(1, size.rounded()), weight: weight)
    : .custom(family, fixedSize: max(1, size.rounded())).weight(weight)
}

private func measure(_ context: GraphicsContext, _ text: String, _ font: SwiftUI.Font) -> CGSize {
  context.resolve(Text(text).font(font)).measure(
    in: CGSize(width: CGFloat.infinity, height: .infinity))
}

/// The window shadow: rings that fade out, under the window's shape.
private func paintShadow(_ context: inout GraphicsContext, _ window: CGRect, _ corner: Double,
                         _ look: WindowLook) {
  let radius = Int(look.shadowRadius.rounded())
  guard radius > 0, look.shadowOpacity > 0 else { return }
  for i in stride(from: radius, through: 1, by: -1) {
    let fade = 1.0 - Double(i) / Double(radius + 1)
    let ring = window.insetBy(dx: -Double(i), dy: -Double(i))
      .offsetBy(dx: 0, dy: look.shadowOffsetY)
    context.fill(Path(roundedRect: ring, cornerRadius: corner + Double(i)),
                 with: .color(paint(look.shadowColor,
                                    look.shadowOpacity * fade * 2.0 / Double(radius))))
  }
}

private func paintWindow(_ context: inout GraphicsContext, _ area: CGRect, _ look: WindowLook,
                         family: String, suggestion: Bool, kindMark: Bool) {
  let candidates = ["今日はいい天気です", "今日もいい天気です", "キョウハイイテンキ"]
  let descriptions = ["", "", "カタカナ"]
  let candidateFont = previewFont(look.candidateFontSize, family)
  let numberFont = previewFont(look.numberFontSize, family)
  let descriptionFont = previewFont(look.descriptionFontSize, family)
  let footerFont = previewFont(look.footerFontSize, family)

  // Sizes as CandidateView.mm lays the rows out.
  let padding = look.windowPadding
  let rowHeight = look.rowHeight
  let numberWidth = suggestion
    ? 0 : look.rowPadding + max(look.numberWidth, measure(context, "9", numberFont).width)
  let gap = look.rowPadding
  // The candidates end in two spaces, as the renderer appends them.
  var textWidth = measure(context, look.columnMinimumWidth + "  ", candidateFont).width
  for candidate in candidates {
    textWidth = max(textWidth, measure(context, candidate + "  ", candidateFont).width)
  }
  let descriptionWidth = measure(context, descriptions[2], descriptionFont).width + look.rowPadding
  let rowWidth = numberWidth + gap + textWidth + descriptionWidth
  // Suggestions have no footer (engine/engine_output.cc).
  let footerLine = measure(context, "1 / 3", footerFont).height
  let footerHeight = suggestion ? 0 : footerLine + look.footerTopPadding + look.footerBottomPadding
  let width = padding * 2 + rowWidth
  let height = padding * 2 + rowHeight * 3 + footerHeight
  let window = CGRect(x: area.midX - width / 2, y: area.midY - height / 2, width: width,
                      height: height)
  let corner = min(look.cornerRadius, height / 2)

  paintShadow(&context, window, corner, look)
  // Drawn whole, then once with the opacity, as a window is.
  var faded = context
  faded.opacity = look.opacity
  faded.drawLayer { layer in
    layer.drawLayer { p in
      p.clip(to: Path(roundedRect: window, cornerRadius: corner))
      p.fill(Path(window), with: .color(paint(look.background)))
      let inner = window.insetBy(dx: padding, dy: padding)
      for row in 0..<3 {
        let cells = CGRect(x: inner.minX, y: inner.minY + Double(row) * rowHeight,
                           width: rowWidth, height: rowHeight)
        let focused = row == 0
        if focused {
          let shape = Path(roundedRect: cells.insetBy(dx: 0.5, dy: 0.5),
                           cornerRadius: look.rowCornerRadius)
          p.fill(shape, with: .color(paint(look.focusedBackground)))
          if look.focusedBorderAlpha > 0 {
            p.stroke(shape, with: .color(paint(look.focusedBorder, look.focusedBorderAlpha)))
          }
        }
        let secondary = paint(focused ? look.focusedSecondaryText : look.secondaryText)
        if !suggestion {
          p.draw(Text("\(row + 1)").font(numberFont).foregroundColor(secondary),
                 at: CGPoint(x: cells.minX + look.rowPadding, y: cells.midY), anchor: .leading)
        }
        p.draw(Text(candidates[row]).font(candidateFont)
                 .foregroundColor(paint(focused ? look.focusedText : look.text)),
               at: CGPoint(x: cells.minX + numberWidth + gap, y: cells.midY), anchor: .leading)
        if !descriptions[row].isEmpty {
          p.draw(Text(descriptions[row]).font(descriptionFont).foregroundColor(secondary),
                 at: CGPoint(x: cells.maxX - look.rowPadding, y: cells.midY), anchor: .trailing)
        }
      }
      if !suggestion {
        let footerTop = inner.minY + rowHeight * 3
        p.draw(Text("1 / 3").font(footerFont).foregroundColor(paint(look.secondaryText)),
               at: CGPoint(x: inner.maxX - look.rowPadding, y: footerTop + look.footerTopPadding),
               anchor: .topTrailing)
        // The conversion window's dot (candidate_kind_mark), as the renderer draws.
        if kindMark {
          let diameter = 7 * look.sizePercent / 100
          let middle = footerTop + look.footerTopPadding + footerLine / 2
          p.fill(Path(ellipseIn: CGRect(x: inner.minX + look.rowPadding, y: middle - diameter / 2,
                                        width: diameter, height: diameter)),
                 with: .color(paint(look.kindMarkColor)))
        }
      }
    }
    let edge = max(0, corner - 0.5)
    layer.stroke(Path(roundedRect: window.insetBy(dx: 0.5, dy: 0.5), cornerRadius: edge),
                 with: .color(paint(look.border, look.borderAlpha)), lineWidth: 1)
  }
}

private func number(_ values: [String: String], _ key: String) -> Double {
  values[key].flatMap { Double($0) } ?? 0
}

private func color32(_ values: [String: String], _ key: String) -> UInt32 {
  values[key].flatMap { UInt32($0) } ?? 0
}

private func name(_ values: [String: String], _ key: String) -> String {
  values[key]?.trimmingCharacters(in: CharacterSet(charactersIn: "\"")) ?? ""
}

/// A pill as IndicatorWindow.mm draws one: lighter at the top.
private func paintPill(_ context: inout GraphicsContext, _ rect: CGRect, _ corner: Double,
                       _ fill: UInt32, _ opacity: Double) {
  // The panel's system shadow, faintly.
  for i in stride(from: 3.0, through: 1, by: -1) {
    context.fill(Path(roundedRect: rect.insetBy(dx: -i, dy: -i).offsetBy(dx: 0, dy: 1),
                      cornerRadius: corner + i),
                 with: .color(Color(.sRGB, red: 0, green: 0, blue: 0, opacity: 14.0 / 255)))
  }
  let lighten = { (part: UInt32) -> UInt32 in
    let value = Double(part) / 255
    return UInt32(((value + (1 - value) * 0.11) * 255).rounded())
  }
  let top = lighten((fill >> 16) & 0xff) << 16 | lighten((fill >> 8) & 0xff) << 8
    | lighten(fill & 0xff)
  context.fill(Path(roundedRect: rect, cornerRadius: corner),
               with: .linearGradient(Gradient(colors: [paint(top, opacity), paint(fill, opacity)]),
                                     startPoint: CGPoint(x: rect.minX, y: rect.minY),
                                     endPoint: CGPoint(x: rect.minX, y: rect.maxY)))
}

private func paintRuby(_ context: inout GraphicsContext, _ area: CGRect,
                       _ values: [String: String], dark: Bool) {
  let lineFont = SwiftUI.Font.system(size: 22)
  let line = "今日は"
  let lineSize = measure(context, line, lineFont)
  let rubyFont = previewFont(number(values, "ruby_style.font_size"))
  let reading = "きょうは"
  let readingSize = measure(context, reading, rubyFont)
  // As IndicatorWindow::UpdateRuby sizes it and LayoutRuby places it.
  let width = readingSize.width + 8
  let height = readingSize.height + 2
  let lineRect = CGRect(x: area.midX - 60, y: area.midY - lineSize.height / 2 + 20,
                        width: lineSize.width, height: lineSize.height)
  let ruby = CGRect(x: lineRect.minX, y: lineRect.minY - 2 - height, width: width, height: height)
  paintPill(&context, ruby, min(number(values, "ruby_style.corner_radius"), height / 2),
            color32(values, "ruby_style.background_color"),
            number(values, "ruby_style.opacity_percent") / 100)
  context.draw(Text(reading).font(rubyFont)
                 .foregroundColor(paint(color32(values, "ruby_style.text_color"))),
               at: CGPoint(x: ruby.midX, y: ruby.midY), anchor: .center)
  context.draw(Text(line).font(lineFont).underline()
                 .foregroundColor(dark ? paint(0xe8e8ea) : paint(0x1d1d1f)),
               at: CGPoint(x: lineRect.minX, y: lineRect.midY), anchor: .leading)
}

private func paintIndicators(_ context: inout GraphicsContext, _ area: CGRect,
                             _ values: [String: String], label: (Int, String) -> String) {
  // IndicatorWindow.mm's measures.
  let dotSize = 5.0, dotLeft = 9.0, dotRoom = 12.0, dividerGap = 6.0, endPadding = 8.0
  let p = "indicator_style."
  let shape = name(values, p + "shape")
  let labelName = name(values, p + "label")
  // (state, english, color field)
  // The last, with あ / A, only where mixed English input shows it.
  let pills: [(Int, Bool, String)] = [
    (1, false, "reading_color"), (2, false, "mozc_color"), (3, false, "zenz_pending_color"),
    (4, false, "zenz_color"),
  ] + (values["mixed_english"] == "true" && values["mixed_english_in_indicator"] == "true"
       ? [(1, true, "reading_color")] : [])
  let font = previewFont(number(values, p + "font_size"), weight: .medium)
  let height = number(values, p + "height")
  let step = height + 12
  var y = area.midY - (step * Double(pills.count) - 12) / 2
  let text = color32(values, p + "text_color")
  for (state, english, field) in pills {
    let shown = label(state, labelName)
    let mode = english && !shown.isEmpty ? "A" : ""
    let dot = labelName == "FULL_LABEL" && shape != "CIRCLE" && shown.count > 1
    let modeWidth = mode.isEmpty ? 0 : measure(context, mode, font).width
    let modeRoom = mode.isEmpty ? 0 : dividerGap * 2 + 1 + modeWidth
    let labelWidth = measure(context, shown, font).width + (dot ? dotRoom : 0) + modeRoom
    // As LayoutIndicator sizes it.
    let width = shape == "CIRCLE"
      ? height : max(number(values, p + "width"), labelWidth + 2 * 8)
    let rect = CGRect(x: area.midX - width / 2, y: y, width: width, height: height)
    let corner = shape == "ROUNDED_RECT"
      ? min(number(values, p + "corner_radius"), height / 2) : height / 2
    paintPill(&context, rect, corner, color32(values, p + field),
              number(values, p + "opacity_percent") / 100)
    let labelRight = rect.maxX - modeRoom
    if dot {
      context.fill(Path(ellipseIn: CGRect(x: rect.minX + dotLeft, y: rect.midY - dotSize / 2,
                                          width: dotSize, height: dotSize)),
                   with: .color(paint(text, 0.85)))
    }
    let room = dot ? dotRoom : 0
    context.draw(Text(shown).font(font).foregroundColor(paint(text)),
                 at: CGPoint(x: (rect.minX + room + labelRight) / 2, y: rect.midY),
                 anchor: .center)
    if !mode.isEmpty {
      let modeLeft = rect.maxX - endPadding - modeWidth
      context.fill(Path(CGRect(x: modeLeft - dividerGap - 1, y: rect.minY + height * 0.22,
                               width: 1, height: height * 0.56)),
                   with: .color(paint(text, 0.45)))
      context.draw(Text(mode).font(font).foregroundColor(paint(text)),
                   at: CGPoint(x: modeLeft + modeWidth / 2, y: rect.midY), anchor: .center)
    }
    y += step
  }
}
