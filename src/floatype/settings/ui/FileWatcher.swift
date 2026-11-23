// floatype: notices that a file the settings show (config.toml,
// english_words.txt) was edited outside the window, so that an open window
// shows it. Each file's modification time, size and inode are looked at a few
// times a second, and a change is told once they hold still, so that a save in
// progress is not read half written. It sees a save that replaces the file
// and one that writes it in place, and follows a link.

import Foundation

public final class FileWatcher {
  /// Posted with the file's path as the object when a file changed.
  public static let changed = Notification.Name("FloatypeSettingsFileChanged")

  private struct Stamp: Equatable {
    let seconds: Int
    let nanoseconds: Int
    let size: Int64
    let inode: UInt64
  }

  private let paths: [String]
  private var seen: [String: Stamp?] = [:]
  private var moving: [String: Stamp?] = [:]
  private var timer: Timer?

  public init(paths: [String]) {
    self.paths = paths
    for path in paths {
      seen[path] = FileWatcher.stamp(path)
    }
  }

  deinit { timer?.invalidate() }

  /// Looks every `interval` seconds on the main run loop.
  public func start(interval: TimeInterval = 0.3) {
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
      self?.check()
    }
  }

  /// Posts `changed` for each file that changed and then held still since the
  /// look before.
  public func check() {
    for path in paths {
      let now = FileWatcher.stamp(path)
      if now == seen[path] ?? nil {
        moving[path] = nil
      } else if let last = moving[path], last == now {
        seen[path] = now
        moving[path] = nil
        NotificationCenter.default.post(name: FileWatcher.changed, object: path)
      } else {
        moving[path] = now
      }
    }
  }

  private static func stamp(_ path: String) -> Stamp? {
    var info = stat()
    guard stat(path, &info) == 0 else { return nil }
    return Stamp(seconds: info.st_mtimespec.tv_sec, nanoseconds: info.st_mtimespec.tv_nsec,
                 size: Int64(info.st_size), inode: UInt64(info.st_ino))
  }
}
