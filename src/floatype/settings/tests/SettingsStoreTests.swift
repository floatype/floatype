import CoreGraphics
import FloatypeSettingsUI
import XCTest

final class SettingsStoreTests: XCTestCase {
  func testReadsTheLines() {
    let store = SettingsStore(
      lines: "use_zenz = true\nindicator_style.position = \"BELOW\"\nzenz_delay_millisec = 500\n",
      save: { _ in nil })
    XCTAssertTrue(store.loaded)
    XCTAssertTrue(store.bool("use_zenz").wrappedValue)
    XCTAssertEqual(store.choice("indicator_style.position").wrappedValue, "BELOW")
    XCTAssertEqual(store.number("zenz_delay_millisec").wrappedValue, 500)
  }

  func testSavesOneLineAChange() {
    var saved: [String] = []
    let store = SettingsStore(lines: "use_zenz = true\n", save: { saved.append($0); return nil })
    store.bool("use_zenz").wrappedValue = false
    store.bool("use_zenz").wrappedValue = false
    store.choice("preedit_method").wrappedValue = "KANA"
    XCTAssertEqual(saved, ["use_zenz = false\n", "preedit_method = \"KANA\"\n"])
    XCTAssertFalse(store.bool("use_zenz").wrappedValue)
  }

  func testSavesAColorOnceItStopsChanging() {
    var saved: [String] = []
    let store = SettingsStore(lines: "ruby_style.text_color = 0\n",
                              save: { saved.append($0); return nil })
    let color = store.color("ruby_style.text_color", shown: 0)
    color.wrappedValue = CGColor(srgbRed: 1, green: 0, blue: 0, alpha: 1)
    color.wrappedValue = CGColor(srgbRed: 0, green: 0, blue: 1, alpha: 1)
    XCTAssertEqual(store.values["ruby_style.text_color"], "255", "shown at once")
    XCTAssertTrue(saved.isEmpty, "not saved while it changes")
    let deadline = Date().addingTimeInterval(2)
    while saved.isEmpty && Date() < deadline {
      RunLoop.main.run(until: Date().addingTimeInterval(0.05))
    }
    XCTAssertEqual(saved, ["ruby_style.text_color = 255\n"])
  }

  func testPutsBackAColorNotSaved() {
    let store = SettingsStore(lines: "ruby_style.text_color = 0\n", save: { _ in "だめ" })
    store.numberSoon("ruby_style.text_color").wrappedValue = 255
    store.savePending()
    XCTAssertEqual(store.values["ruby_style.text_color"], "0")
    XCTAssertEqual(store.error, "だめ")
  }

  func testShowsAnUnsetColorAsGiven() {
    let store = SettingsStore(lines: "", save: { _ in nil })
    let color = store.color("candidate_style.text_color", shown: 0x123456).wrappedValue
    XCTAssertEqual(color.components?.map { Int(($0 * 255).rounded()) }, [0x12, 0x34, 0x56, 255])
    XCTAssertNil(store.values["candidate_style.text_color"], "still unset")
  }

  func testKeepsTheValueWhenNotSaved() {
    let store = SettingsStore(lines: "use_zenz = true\n", save: { _ in "だめ" })
    store.bool("use_zenz").wrappedValue = false
    XCTAssertTrue(store.bool("use_zenz").wrappedValue)
    XCTAssertEqual(store.error, "だめ")
  }

  func testFlagsAndCharacters() {
    var saved: [String] = []
    let store = SettingsStore(
      lines: "auto_conversion_key = 13\ndirect_commit_characters = \"、。\"\n",
      save: { saved.append($0); return nil })
    XCTAssertTrue(store.flag("auto_conversion_key", 4).wrappedValue)
    XCTAssertFalse(store.flag("auto_conversion_key", 2).wrappedValue)
    store.flag("auto_conversion_key", 2).wrappedValue = true
    store.flag("auto_conversion_key", 1).wrappedValue = false
    XCTAssertEqual(store.number("auto_conversion_key").wrappedValue, 14)
    XCTAssertTrue(store.contains("direct_commit_characters", "。").wrappedValue)
    store.contains("direct_commit_characters", "。").wrappedValue = false
    store.contains("direct_commit_characters", "！").wrappedValue = true
    XCTAssertEqual(store.text("direct_commit_characters").wrappedValue, "、！")
    XCTAssertEqual(saved.last, "direct_commit_characters = \"、！\"\n")
  }

  func testQuotesAndBackslashesInText() {
    var saved: [String] = []
    let store = SettingsStore(lines: "", save: { saved.append($0); return nil })
    store.text("candidate_style.font_family").wrappedValue = "a\"b\\c"
    XCTAssertEqual(saved, ["candidate_style.font_family = \"a\\\"b\\\\c\"\n"])
    XCTAssertEqual(store.text("candidate_style.font_family").wrappedValue, "a\"b\\c")
  }

  func testResetReadsTheSettingsAgain() {
    var resets = 0
    var actions = SettingsActions.none()
    actions.reset = { resets += 1; return nil }
    actions.load = { "use_zenz = true\n" }
    let store = SettingsStore(lines: "use_zenz = false\n", save: { _ in nil }, actions: actions)
    store.reset()
    XCTAssertEqual(resets, 1)
    XCTAssertTrue(store.bool("use_zenz").wrappedValue)

    actions.reset = { "だめ" }
    let failing = SettingsStore(lines: "use_zenz = false\n", save: { _ in nil }, actions: actions)
    failing.reset()
    XCTAssertEqual(failing.error, "だめ")
    XCTAssertFalse(failing.bool("use_zenz").wrappedValue)
  }

  func testWithoutSettingsShowsNothing() {
    XCTAssertFalse(SettingsStore(lines: nil, save: { _ in nil }).loaded)
  }
}
