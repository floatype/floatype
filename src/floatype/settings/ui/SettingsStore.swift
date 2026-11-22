// floatype: the settings as the screen shows them (issue #9). Values are kept
// as TOML values ("true", "228", "\"ROMAN\""), keyed by the field's dotted
// name, as the bridge's lines give them; each change is saved at once, as
// one line naming only that field.

import Combine
import SwiftUI

public final class SettingsStore: ObservableObject {
  @Published public private(set) var values: [String: String] = [:]
  /// Why the last change was not saved, shown until the next change.
  @Published public var error: String?

  private let save: (String) -> String?
  public let actions: SettingsActions

  /// Whether the settings could be read; without them nothing is shown, so
  /// that nothing shown as off or 0 can be saved.
  @Published public private(set) var loaded = false

  /// `lines` as the bridge loads them, nil if it could not; `save` takes TOML
  /// and returns why it failed, or nil.
  public init(lines: String?, save: @escaping (String) -> String?,
              actions: SettingsActions = .none()) {
    self.save = save
    self.actions = actions
    read(lines)
  }

  /// Puts the converter's settings back to the defaults, and shows them.
  public func reset() {
    if let failure = actions.reset() {
      error = failure
    } else {
      error = nil
      read(actions.load())
    }
  }

  /// Runs an action, showing why it failed.
  public func run(_ action: () -> String?) {
    error = action()
  }

  private func read(_ lines: String?) {
    // What waited to be saved is not saved over what is read.
    flush?.cancel()
    pending = [:]
    loaded = lines != nil
    guard let lines else { return }
    var read: [String: String] = [:]
    for line in lines.split(separator: "\n") {
      guard let equals = line.range(of: " = ") else { continue }
      read[String(line[..<equals.lowerBound])] = String(line[equals.upperBound...])
    }
    values = read
    saved = read
  }

  public func bool(_ key: String) -> Binding<Bool> {
    Binding(
      get: { self.values[key] == "true" },
      set: { self.set(key, $0 ? "true" : "false") })
  }

  /// An enum's value name.
  public func choice(_ key: String) -> Binding<String> {
    Binding(
      get: { self.values[key].map { $0.trimmingCharacters(in: CharacterSet(charactersIn: "\"")) } ?? "" },
      set: { self.set(key, "\"\($0)\"") })
  }

  public func number(_ key: String) -> Binding<Int> {
    Binding(
      get: { self.values[key].flatMap { Int($0) } ?? 0 },
      set: { self.set(key, String($0)) })
  }

  /// A string; config.proto's strings here are short and plain, so only
  /// \" and \\ are escaped.
  public func text(_ key: String) -> Binding<String> {
    Binding(
      get: {
        guard let value = self.values[key], value.count >= 2 else { return "" }
        return String(value.dropFirst().dropLast())
          .replacingOccurrences(of: "\\\"", with: "\"")
          .replacingOccurrences(of: "\\\\", with: "\\")
      },
      set: {
        let escaped = $0.replacingOccurrences(of: "\\", with: "\\\\")
          .replacingOccurrences(of: "\"", with: "\\\"")
        self.set(key, "\"\(escaped)\"")
      })
  }

  /// One bit of a number holding flags (auto_conversion_key).
  public func flag(_ key: String, _ bit: Int) -> Binding<Bool> {
    let number = self.number(key)
    return Binding(
      get: { number.wrappedValue & bit != 0 },
      set: { number.wrappedValue = $0 ? number.wrappedValue | bit : number.wrappedValue & ~bit })
  }

  /// Whether the string `key` holds `character` (direct_commit_characters).
  public func contains(_ key: String, _ character: Character) -> Binding<Bool> {
    let text = self.text(key)
    return Binding(
      get: { text.wrappedValue.contains(character) },
      set: {
        let rest = text.wrappedValue.filter { $0 != character }
        text.wrappedValue = $0 ? rest + String(character) : rest
      })
  }

  /// 一般's 数字の入力, "0" to "3" (see SettingsActions.numberForm); choosing
  /// one saves character_form_rules whole, the other groups' rules as they
  /// are. "3" sets nothing.
  public func numberForm() -> Binding<String> {
    let key = "character_form_rules"
    let line = { "\(key) = \(self.values[key] ?? "[]")\n" }
    return Binding(
      get: { String(self.actions.numberForm(line())) },
      set: {
        guard let form = Int($0), let rules = self.actions.withNumberForm(line(), form) else {
          return
        }
        self.set(key, rules)
      })
  }

  /// The settings shown, as lines, for what reads them whole (外観's preview).
  public var lines: String {
    values.map { "\($0.key) = \($0.value)\n" }.joined()
  }

  /// A number shown at once and saved once it stops changing (a slider).
  public func numberSoon(_ key: String) -> Binding<Double> {
    Binding(
      get: { self.values[key].flatMap { Double($0) } ?? 0 },
      set: { self.setSoon(key, String(Int($0.rounded()))) })
  }

  /// A color, 0xRRGGBB, shown at once and saved once it stops changing (one
  /// dragged in the color panel); `shown` gives it while unset.
  public func color(_ key: String, shown: UInt32) -> Binding<CGColor> {
    Binding(
      get: {
        let rgb = self.values[key].flatMap { UInt32($0) } ?? shown
        return CGColor(
          srgbRed: CGFloat((rgb >> 16) & 0xff) / 255, green: CGFloat((rgb >> 8) & 0xff) / 255,
          blue: CGFloat(rgb & 0xff) / 255, alpha: 1)
      },
      set: {
        // The panel can hand back the color shown: an unset one stays unset.
        let current = self.values[key].flatMap { UInt32($0) } ?? shown
        guard let srgb = $0.converted(
          to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
          let parts = srgb.components, parts.count >= 3 else { return }
        let byte = { (part: CGFloat) in UInt32((min(max(part, 0), 1) * 255).rounded()) }
        let rgb = byte(parts[0]) << 16 | byte(parts[1]) << 8 | byte(parts[2])
        guard rgb != current else { return }
        self.setSoon(key, String(rgb))
      })
  }

  private func set(_ key: String, _ value: String) {
    guard values[key] != value else { return }
    if let failure = save("\(key) = \(value)\n") {
      error = failure
    } else {
      error = nil
      values[key] = value
      saved[key] = value
    }
  }

  /// What the converter has, for putting back what failed to save.
  private var saved: [String: String] = [:]
  private var pending: [String: String] = [:]
  private var flush: DispatchWorkItem?

  private func setSoon(_ key: String, _ value: String) {
    guard values[key] != value else { return }
    values[key] = value
    pending[key] = value
    flush?.cancel()
    let work = DispatchWorkItem { [weak self] in self?.savePending() }
    flush = work
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
  }

  /// Saves what changed soon, now; also before the app quits.
  public func savePending() {
    flush?.cancel()
    guard !pending.isEmpty else { return }
    let changes = pending
    pending = [:]
    if let failure = save(changes.map { "\($0.key) = \($0.value)\n" }.joined()) {
      error = failure
      for key in changes.keys { values[key] = saved[key] }
    } else {
      error = nil
      saved.merge(changes) { $1 }
    }
  }
}
