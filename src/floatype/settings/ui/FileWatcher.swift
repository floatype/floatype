// floatype: notices that a file the settings show (config.toml,
// english_words.txt) was edited outside the window, so that an open window
// shows it. FSEvents on the files' folders (and, for a link, the folder it
// points into) says when to look; a slow timer looks too, for an event missed
// and a link pointed elsewhere. A look compares each file's modification time,
// size and inode, and a change is told once they hold still for a look 0.3
// seconds later, so that a save in progress is not read half written. It sees
// a save that replaces the file and one that writes it in place.

import CoreServices
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
  private var settling = false
  private var timer: Timer?
  private var stream: FSEventStreamRef?

  public init(paths: [String]) {
    self.paths = paths
    for path in paths {
      seen[path] = FileWatcher.stamp(path)
    }
  }

  deinit {
    timer?.invalidate()
    if let stream {
      FSEventStreamStop(stream)
      FSEventStreamInvalidate(stream)
      FSEventStreamRelease(stream)
    }
  }

  /// Listens to FSEvents, and looks every `interval` seconds as well, on the
  /// main queue.
  public func start(interval: TimeInterval = 2) {
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
      self?.lookUntilStill()
    }
    timer?.tolerance = interval / 2
    guard stream == nil else { return }
    let folders = Set(paths.flatMap { path in
      [path, (path as NSString).resolvingSymlinksInPath].map {
        ($0 as NSString).deletingLastPathComponent
      }
    })
    var context = FSEventStreamContext(
      version: 0, info: Unmanaged.passUnretained(self).toOpaque(),
      retain: nil, release: nil, copyDescription: nil)
    // Which file changed is left to the look: it compares them all.
    let callback: FSEventStreamCallback = { _, info, _, _, _, _ in
      guard let info else { return }
      Unmanaged<FileWatcher>.fromOpaque(info).takeUnretainedValue().lookUntilStill()
    }
    guard let made = FSEventStreamCreate(
      nil, callback, &context, Array(folders) as CFArray,
      FSEventStreamEventId(kFSEventStreamEventIdSinceNow), 0.1,
      FSEventStreamCreateFlags(kFSEventStreamCreateFlagNoDefer))
    else { return }
    FSEventStreamSetDispatchQueue(made, DispatchQueue.main)
    FSEventStreamStart(made)
    stream = made
  }

  /// Looks, and while a file is still changing, looks again 0.3 seconds
  /// later. Events and the timer both come here, so that two looks closer
  /// than that never count a file as still.
  public func lookUntilStill() {
    guard !settling else { return }
    check()
    guard !moving.isEmpty else { return }
    settling = true
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
      self?.settling = false
      self?.lookUntilStill()
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
