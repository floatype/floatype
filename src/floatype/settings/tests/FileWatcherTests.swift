import FloatypeSettingsUI
import Foundation
import XCTest

final class FileWatcherTests: XCTestCase {
  func testTellsAChangeOnceItHoldsStill() throws {
    let folder = FileManager.default.temporaryDirectory
      .appendingPathComponent("floatype-watch-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: folder) }
    let file = folder.appendingPathComponent("config.toml")
    try "use_zenz = true\n".write(to: file, atomically: true, encoding: .utf8)

    var told: [String] = []
    let observer = NotificationCenter.default.addObserver(
      forName: FileWatcher.changed, object: nil, queue: nil) { told.append($0.object as! String) }
    defer { NotificationCenter.default.removeObserver(observer) }
    let watcher = FileWatcher(paths: [file.path])
    watcher.check()
    XCTAssertEqual(told, [], "nothing changed")

    // Replaced, as editors save.
    try "use_zenz = false\n".write(to: file, atomically: true, encoding: .utf8)
    watcher.check()
    XCTAssertEqual(told, [], "not read while it may still be written")
    watcher.check()
    XCTAssertEqual(told, [file.path])
    watcher.check()
    XCTAssertEqual(told.count, 1, "told once")

    // As events and the timer look: once, then again 0.3 seconds later.
    try "use_zenz = true\n".write(to: file, atomically: true, encoding: .utf8)
    watcher.lookUntilStill()
    XCTAssertEqual(told.count, 1, "not read at the first look")
    let deadline = Date().addingTimeInterval(3)
    while told.count < 2 && Date() < deadline {
      RunLoop.main.run(until: Date().addingTimeInterval(0.05))
    }
    XCTAssertEqual(told.count, 2)

    // Removed counts too.
    try FileManager.default.removeItem(at: file)
    watcher.check()
    watcher.check()
    XCTAssertEqual(told.count, 3)
  }
}
