// Assert-based tests for zenz.h. Run with ctest, or build/zenz_test directly.

#include "zenz.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

const std::string kIn = "\xEE\xB8\x80";     // U+EE00
const std::string kOut = "\xEE\xB8\x81";    // U+EE01
const std::string kCtx = "\xEE\xB8\x82";    // U+EE02
const std::string kSpace = "\xE3\x80\x80";  // U+3000

void TestPromptWithoutContext() {
  Expect(floatype::zenz::BuildPrompt("", "キョウ") == kIn + "キョウ" + kOut,
         "no context tag without context");
}

void TestPromptWithContext() {
  Expect(floatype::zenz::BuildPrompt("熱が ある\n", "イシャ") ==
             kCtx + "熱が" + kSpace + "ある" + kIn + "イシャ" + kOut,
         "context is tagged, spaces become U+3000 and line breaks are dropped");
}

void TestPromptKeepsLast40Characters() {
  std::string context;
  for (int i = 0; i < 30; ++i) context += "x";
  std::string last40;
  for (int i = 0; i < 40; ++i)
    last40 += "あ";  // Multi-byte, so bytes != characters.
  Expect(floatype::zenz::BuildPrompt(context + last40, "ア") ==
             kCtx + last40 + kIn + "ア" + kOut,
         "only the last 40 characters of the context are kept");
}

void TestFindStop() {
  using floatype::zenz::FindStop;
  constexpr size_t npos = std::string_view::npos;
  Expect(FindStop("今日は") == npos, "plain text has no stop");
  Expect(FindStop("今日" + kIn + "x") == std::string("今日").size(),
         "stops at U+EE00");
  Expect(FindStop("a\xEE\xB8\x8F") == 1, "stops at U+EE0F");
  Expect(FindStop("a\xEE\xB8\x90") == npos, "U+EE10 is not a zenz tag");
  Expect(FindStop("a\xEE\xB8") == npos, "an incomplete tag is not a stop yet");
  Expect(FindStop("ab\ncd") == 2, "stops at a line feed");
  Expect(FindStop("ab\rcd") == 2, "stops at a carriage return");
}

}  // namespace

int main() {
  TestPromptWithoutContext();
  TestPromptWithContext();
  TestPromptKeepsLast40Characters();
  TestFindStop();
  if (failures == 0) {
    std::puts("zenz_test: all passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
