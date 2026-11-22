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

  func testKeepsTheValueWhenNotSaved() {
    let store = SettingsStore(lines: "use_zenz = true\n", save: { _ in "だめ" })
    store.bool("use_zenz").wrappedValue = false
    XCTAssertTrue(store.bool("use_zenz").wrappedValue)
    XCTAssertEqual(store.error, "だめ")
  }

  func testWithoutSettingsShowsNothing() {
    XCTAssertFalse(SettingsStore(lines: nil, save: { _ in nil }).loaded)
  }
}
