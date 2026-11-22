// floatype: 更新を確認 (issue #9), as the Qt screen does it: the one place the
// settings reach the network, and only when the button is pressed. curl asks
// GitHub over https alone, on redirects too; a newer package is downloaded
// with its .sha256, checked, and opened in the Installer.

import AppKit
import CryptoKit
import Foundation

public final class Updater: ObservableObject {
  @Published public private(set) var status: String
  @Published public private(set) var busy = false
  @Published public private(set) var newer: Release?

  private let actions: SettingsActions

  public init(actions: SettingsActions) {
    self.actions = actions
    status = "今の版は \(actions.version)。押したときだけ GitHub に問い合わせます"
  }

  public var buttonTitle: String { newer == nil ? "更新を確認" : "ダウンロードして入れる" }

  public func press() {
    if case .newer(_, let package, let checksum) = newer {
      download(package: package, checksum: checksum)
    } else {
      check()
    }
  }

  private func check() {
    show("確認しています…", busy: true)
    curl(["-H", "Accept: application/vnd.github+json", "-w", "\n%{http_code}",
          actions.latestReleaseURL], seconds: 20) { ok, output in
      // The status is the last line; the answer comes before it.
      let text = String(decoding: output, as: UTF8.self)
      let end = text.lastIndex(of: "\n")
      let status = ok ? Int(text[(end.map { text.index(after: $0) } ?? text.endIndex)...]
        .trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0 : 0
      let body = Data(text[..<(end ?? text.startIndex)].utf8)
      let release = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any] ?? [:]
      let assets = (release["assets"] as? [[String: Any]] ?? []).map {
        ($0["name"] as? String ?? "", $0["browser_download_url"] as? String ?? "")
      }
      let answer = self.actions.checkRelease(status, release["tag_name"] as? String ?? "", assets)
      switch answer {
      case .newer(let version, _, _):
        self.newer = answer
        self.show("新しい版 \(version) があります（今は \(self.actions.version)）", busy: false)
      case .upToDate: self.show("最新です", busy: false)
      case .noRelease: self.show("まだリリースがありません", busy: false)
      case .failed: self.show("確認できませんでした", busy: false)
      }
    }
  }

  private func download(package: URL, checksum: URL) {
    show("ダウンロードしています…", busy: true)
    // Kept after the window closes: the Installer reads the package from it.
    let folder = FileManager.default.temporaryDirectory
      .appendingPathComponent("floatype-update-\(UUID().uuidString)")
    try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    let packageFile = folder.appendingPathComponent("floatype.pkg")
    let checksumFile = folder.appendingPathComponent("floatype.pkg.sha256")
    let failed = { (text: String) in
      try? FileManager.default.removeItem(at: folder)
      self.newer = nil
      self.show(text, busy: false)
    }
    curl(["-f", "-o", checksumFile.path, checksum.absoluteString], seconds: 60) { ok, _ in
      guard ok else { return failed("ダウンロードできませんでした") }
      self.curl(["-f", "-o", packageFile.path, package.absoluteString], seconds: 900) { ok, _ in
        guard ok, let data = try? Data(contentsOf: packageFile),
              let named = try? String(contentsOf: checksumFile, encoding: .utf8)
        else { return failed("ダウンロードできませんでした") }
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        guard self.actions.checksumMatches(named, digest) else {
          return failed("ダウンロードした物の SHA-256 が合わないので、消しました")
        }
        // The Installer replaces this window too.
        NSWorkspace.shared.open(packageFile)
        NSApp.terminate(nil)
      }
    }
  }

  private func show(_ text: String, busy: Bool) {
    status = text
    self.busy = busy
  }

  /// Runs curl off the main thread and calls `done` on it.
  private func curl(_ arguments: [String], seconds: Int,
                    done: @escaping (Bool, Data) -> Void) {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/bin/curl")
    process.arguments = ["-sS", "-L", "--proto", "=https", "--proto-redir", "=https",
                         "--max-time", String(seconds)] + arguments
    let pipe = Pipe()
    process.standardOutput = pipe
    process.standardError = FileHandle.nullDevice
    DispatchQueue.global().async {
      var ok = false
      var output = Data()
      if (try? process.run()) != nil {
        output = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        ok = process.terminationStatus == 0
      }
      DispatchQueue.main.async { done(ok, output) }
    }
  }
}
