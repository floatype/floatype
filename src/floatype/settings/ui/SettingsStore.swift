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

  /// `lines` as the bridge loads them; `save` takes TOML and returns why it
  /// failed, or nil.
  public init(lines: String, save: @escaping (String) -> String?) {
    self.save = save
    for line in lines.split(separator: "\n") {
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
