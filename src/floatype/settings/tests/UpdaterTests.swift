import CryptoKit
import FloatypeSettingsUI
import Foundation
import XCTest

/// curl swapped for one that answers from here and writes the files asked
/// for, so that no test reaches the network or opens the Installer.
final class FakeCurl {
  var answer = "{}\n404"
  var package = Data("pkg".utf8)
  var checksum: String?  // Defaults to the package's.
  var downloads = true
  var calls: [[String]] = []

  func run(_ arguments: [String], _ seconds: Int, _ done: @escaping (Bool, Data) -> Void) {
    calls.append(arguments)
    guard let output = arguments.firstIndex(of: "-o").map({ arguments[$0 + 1] }) else {
      return done(true, Data(answer.utf8))
    }
    guard downloads else { return done(false, Data()) }
    let hex = SHA256.hash(data: package).map { String(format: "%02x", $0) }.joined()
    let data = output.hasSuffix(".sha256")
      ? Data("\(checksum ?? hex)  floatype.pkg\n".utf8) : package
    FileManager.default.createFile(atPath: output, contents: data)
    done(true, Data())
  }
}

final class UpdaterTests: XCTestCase {
  let fake = FakeCurl()
  var opened: [URL] = []

  func updater() -> Updater {
    var actions = SettingsActions.none(version: "0.1.1")
    actions.latestReleaseURL = "https://example.invalid/latest"
    actions.checkRelease = { status, _, _ in
      switch status {
      case 200:
        return .newer(version: "0.2.0", package: URL(string: "https://example.invalid/p.pkg")!,
                      checksum: URL(string: "https://example.invalid/p.pkg.sha256")!)
      case 404: return .noRelease
      default: return .failed
      }
    }
    actions.checksumMatches = { file, hex in file.hasPrefix(hex) }
    return Updater(actions: actions, curl: fake.run, openPackage: { self.opened.append($0) })
  }

  /// Runs the main loop until `done` holds, as the updater calls back there.
  func wait(until done: () -> Bool) {
    let deadline = Date().addingTimeInterval(5)
    while !done() && Date() < deadline {
      RunLoop.main.run(until: Date().addingTimeInterval(0.01))
    }
  }

  func testSplitsTheVersion() {
    let both = SettingsActions.none(version: "0.1.5+mozc.3.35.6354")
    XCTAssertEqual(both.floatypeVersion, "0.1.5")
    XCTAssertEqual(both.mozcVersion, "3.35.6354")
    let alone = SettingsActions.none(version: "0.1.5")
    XCTAssertEqual(alone.floatypeVersion, "0.1.5")
    XCTAssertNil(alone.mozcVersion)
    XCTAssertEqual(updater().status, Updater.idle)
  }

  func testNoReleaseYet() {
    let updater = updater()
    updater.press()
    wait { !updater.busy }
    XCTAssertEqual(updater.status, "まだリリースがありません")
    XCTAssertEqual(fake.calls.first?.last, "https://example.invalid/latest")
  }

  func testDownloadsAndOpensANewerPackage() {
    fake.answer = "{\"tag_name\": \"v0.2.0\", \"assets\": []}\n200"
    let updater = updater()
    updater.press()
    wait { !updater.busy }
    XCTAssertEqual(updater.buttonTitle, "ダウンロードして入れる")
    updater.press()
    wait { !opened.isEmpty }
    XCTAssertEqual(opened.first?.lastPathComponent, "floatype.pkg")
  }

  func testRemovesAPackageWhoseChecksumDiffers() {
    fake.answer = "{\"tag_name\": \"v0.2.0\"}\n200"
    fake.checksum = String(repeating: "0", count: 64)
    let updater = updater()
    updater.press()
    wait { !updater.busy }
    updater.press()
    wait { !updater.busy }
    XCTAssertTrue(opened.isEmpty)
    XCTAssertEqual(updater.status, "ダウンロードした物の SHA-256 が合わないので、消しました")
    let last = fake.calls.last!
    let package = last[last.firstIndex(of: "-o")! + 1]
    XCTAssertFalse(FileManager.default.fileExists(
      atPath: URL(fileURLWithPath: package).deletingLastPathComponent().path))
    XCTAssertEqual(updater.buttonTitle, "更新を確認")
  }

  func testSaysWhenTheDownloadFails() {
    fake.answer = "{\"tag_name\": \"v0.2.0\"}\n200"
    fake.downloads = false
    let updater = updater()
    updater.press()
    wait { !updater.busy }
    updater.press()
    wait { !updater.busy }
    XCTAssertTrue(opened.isEmpty)
    XCTAssertEqual(updater.status, "ダウンロードできませんでした")
  }
}
