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

  /// Whether the settings could be read; without them nothing is shown, so
  /// that nothing shown as off or 0 can be saved.
  public let loaded: Bool

  /// `lines` as the bridge loads them, nil if it could not; `save` takes TOML
  /// and returns why it failed, or nil.
  public init(lines: String?, save: @escaping (String) -> String?) {
    self.save = save
    loaded = lines != nil
    for line in (lines ?? "").split(separator: "\n") {
      guard let equals = line.range(of: " = ") else { continue }
      values[String(line[..<equals.lowerBound])] = String(line[equals.upperBound...])
    }
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

  private func set(_ key: String, _ value: String) {
    guard values[key] != value else { return }
    if let failure = save("\(key) = \(value)\n") {
      error = failure
    } else {
      error = nil
      values[key] = value
    }
  }
}
