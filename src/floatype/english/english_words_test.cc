#include "floatype/english/english_words.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "floatype/config/settings_file.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

class EnglishWordsTest : public ::testing::Test {
 protected:
  static EnglishWords Words() {
    return EnglishWords("Macbook\nhello\nupdate\nkoto\n");
  }
};

TEST_F(EnglishWordsTest, AWordWithLettersLeftOverIsEnglish) {
  const EnglishWords words = Words();
  EXPECT_TRUE(LooksEnglish("macbook", "まcぼおk", words));
  EXPECT_TRUE(LooksEnglish("update", "うｐだて", words)) << "full width too";
}

TEST_F(EnglishWordsTest, KanaThatReadsWellStaysJapanese) {
  const EnglishWords words = Words();
  EXPECT_FALSE(LooksEnglish("hello", "へっぉ", words)) << "no letters left";
  EXPECT_FALSE(LooksEnglish("koto", "こと", words));
}

TEST_F(EnglishWordsTest, ATypoIsNotAWord) {
  const EnglishWords words = Words();
  EXPECT_FALSE(LooksEnglish("wtasi", "wたし", words));
  EXPECT_FALSE(LooksEnglish("ab", "あb", words)) << "too short";
}

// One composed character per element: what was typed, read and shown.
std::vector<EnglishSpan> Spans(
    const std::vector<std::vector<std::string>>& chars,
    const EnglishWords& words) {
  std::vector<std::string> raws, query, preedit;
  for (const std::vector<std::string>& c : chars) {
    raws.push_back(c[0]);
    query.push_back(c[1]);
    preedit.push_back(c.size() > 2 ? c[2] : c[1]);
  }
  return EnglishSpans(raws, query, preedit, words);
}

using Spans_ = std::vector<EnglishSpan>;

// A dictionary with words of these readings, for ReadsAsJapanese().
ReadingLookup ReadingsOf(std::vector<std::string> readings) {
  return [readings = std::move(readings)](
             absl::string_view key,
             absl::FunctionRef<void(absl::string_view)> reading) {
    for (const std::string& r : readings) {
      if (key.starts_with(r)) {
        reading(r);
      }
    }
  };
}

TEST_F(EnglishWordsTest, KanaReadAsJapaneseByWordsAndParticles) {
  const ReadingLookup lookup =
      ReadingsOf({"まで", "ぺん", "おん", "べ", "れ", "お", "せ", "ば"});
  EXPECT_TRUE(ReadsAsJapanese("まで", lookup));    // made
  EXPECT_TRUE(ReadsAsJapanese("わね", lookup));    // wane: two particles
  EXPECT_TRUE(ReadsAsJapanese("もおん", lookup));  // moon: も + おん
  // Single kana are words of the dictionary too, but nearly any kana is.
  EXPECT_FALSE(ReadsAsJapanese("べふぉれ", lookup));  // before
  EXPECT_FALSE(ReadsAsJapanese("おぺん", lookup));    // open
  EXPECT_FALSE(ReadsAsJapanese("ばせ", lookup));      // base
  EXPECT_TRUE(ReadsAsJapanese("", lookup));
  EXPECT_TRUE(ReadsAsJapanese("べふぉれ", nullptr)) << "no dictionary to ask";
}

TEST_F(EnglishWordsTest, FindsAWordTypedWhollyIntoKana) {
  const EnglishWords words("before\nmade\nafter\ntime\nopen\n", "time\n");
  const auto japanese = [](absl::string_view kana) {
    return kana == "まで" || kana == "ちめ";
  };
  const auto spans = [&](const std::vector<std::vector<std::string>>& chars) {
    std::vector<std::string> raws, query, preedit;
    for (const std::vector<std::string>& c : chars) {
      raws.push_back(c[0]);
      query.push_back(c[1]);
      preedit.push_back(c.size() > 2 ? c[2] : c[1]);
    }
    return EnglishSpans(raws, query, preedit, words, japanese);
  };
  // before, alone and before a symbol.
  EXPECT_EQ(spans({{"be", "べ"}, {"fo", "ふぉ"}, {"re", "れ"}}),
            (Spans_{{0, 3, "before", ""}}));
  EXPECT_EQ(spans({{"be", "べ"}, {"fo", "ふぉ"}, {"re", "れ"}, {"/", "/"},
                   {"a", "あ"}, {"f", "f", "ｆ"}, {"te", "て"},
                   {"r", "r", "ｒ"}}),
            (Spans_{{0, 3, "before", ""}, {4, 8, "after", ""}}));
  // A trailing n is ん in the query.
  EXPECT_EQ(spans({{"o", "お"}, {"pe", "ぺ"}, {"n", "ん", "ｎ"}}),
            (Spans_{{0, 3, "open", ""}}));
  // made reads as Japanese; time does too, but the user added it.
  EXPECT_EQ(spans({{"ma", "ま"}, {"de", "で"}}), Spans_{});
  EXPECT_EQ(spans({{"ti", "ち"}, {"me", "め"}}), (Spans_{{0, 2, "time", ""}}));
  // Inside a longer run of kana the keys are no word.
  EXPECT_EQ(spans({{"be", "べ"}, {"fo", "ふぉ"}, {"re", "れ"}, {"ni", "に"}}),
            Spans_{});
  // Without a way to tell Japanese, kana stay kana.
  EXPECT_EQ(Spans({{"be", "べ"}, {"fo", "ふぉ"}, {"re", "れ"}}, words), Spans_{});
}

TEST_F(EnglishWordsTest, FindsAWordInsideJapanese) {
  const EnglishWords words = Words();
  // きょうは update する
  EXPECT_EQ(Spans({{"kyo", "きょ"}, {"u", "う"}, {"ha", "は"}, {"u", "う"},
                   {"p", "ｐ"}, {"da", "だ"}, {"te", "て"}, {"su", "す"},
                   {"ru", "る"}},
                  words),
            (Spans_{{3, 7, "update", ""}}));
  // update を
  EXPECT_EQ(Spans({{"u", "う"}, {"p", "ｐ"}, {"da", "だ"}, {"te", "て"},
                   {"wo", "を"}},
                  words),
            (Spans_{{0, 4, "update", ""}}));
}

TEST_F(EnglishWordsTest, LeavesAWordAlreadyInLettersAndJapaneseAlone) {
  const EnglishWords words = Words();
  EXPECT_EQ(Spans({{"u", "u"}, {"p", "p"}, {"d", "d"}, {"a", "a"}, {"t", "t"},
                   {"e", "e"}, {"wo", "を"}},
                  words),
            Spans_{});
  EXPECT_EQ(Spans({{"ko", "こ"}, {"to", "と"}}, words), Spans_{});
  EXPECT_EQ(Spans({{"he", "へ"}, {"llo", "っぉ"}}, words), Spans_{});
}

TEST_F(EnglishWordsTest, DoesNotCutAWordShort) {
  const EnglishWords words = Words();
  // "updatex" is no word, and "update" would cut it.
  EXPECT_EQ(Spans({{"u", "う"}, {"p", "ｐ"}, {"da", "だ"}, {"te", "て"},
                   {"x", "ｘ"}},
                  words),
            Spans_{});
}

TEST_F(EnglishWordsTest, ExtendsAWordKeptInLetters) {
  const EnglishWords words = EnglishWords("update\nupdates\n");
  EXPECT_EQ(Spans({{"u", "u"}, {"p", "p"}, {"d", "d"}, {"a", "a"}, {"t", "t"},
                   {"e", "e"}, {"s", "s", "ｓ"}},
                  words),
            (Spans_{{0, 7, "updates", ""}}));
}

TEST_F(EnglishWordsTest, RetypesLettersTheNextKanaTook) {
  const EnglishWords words = EnglishWords("email\n");
  // email を: the romaji held l and w together (lw starts ゎ).
  EXPECT_EQ(Spans({{"e", "え"}, {"ma", "ま"}, {"i", "い"}, {"l", "ｌ"},
                   {"w", "ｗ"}, {"o", "お"}},
                  words),
            (Spans_{{0, 6, "email", "wo"}}));
  // Letters still pending at the end may yet make a longer word.
  EXPECT_EQ(Spans({{"e", "え"}, {"ma", "ま"}, {"i", "い"}, {"l", "ｌ"},
                   {"w", "ｗ"}},
                  words),
            Spans_{});
}

TEST_F(EnglishWordsTest, EndsAWordInsideTheKanaAfterIt) {
  const EnglishWords words = EnglishWords("test\n");
  // test する: t and su made つ.
  EXPECT_EQ(Spans({{"te", "て"}, {"s", "ｓ"}, {"tsu", "つ"}, {"ru", "る"}},
                  words),
            (Spans_{{0, 3, "test", "su"}}));
}

TEST_F(EnglishWordsTest, ALongerWordWins) {
  const EnglishWords words = EnglishWords("has\nslack\nlack\n");
  // は slack: "has" would take the s and leave ck over.
  EXPECT_EQ(Spans({{"ha", "は"}, {"s", "ｓ"}, {"la", "ぁ"}, {"c", "ｃ"},
                   {"k", "ｋ"}, {"su", "す"}},
                  words),
            (Spans_{{1, 5, "slack", ""}}));
}

TEST_F(EnglishWordsTest, DoesNotCutAKanaInTwo) {
  const EnglishWords words = EnglishWords("keyboard\n");
  // keyboard を: dwo made どぉ, its keys split as d|wo.
  EXPECT_EQ(Spans({{"ke", "け"}, {"y", "ｙ"}, {"bo", "ぼ"}, {"a", "あ"},
                   {"r", "ｒ"}, {"d", "ど"}, {"wo", "ぉ"}},
                  words),
            (Spans_{{0, 7, "keyboard", "wo"}}));
}

// Words inside Japanese, typed wholly into kana (KanaSearch).
class KanaSearchTest : public ::testing::Test {
 protected:
  // The spans of `chars` with a dictionary of `readings`.
  static std::vector<EnglishSpan> Spans(
      const std::vector<std::vector<std::string>>& chars,
      const EnglishWords& words, std::vector<std::string> readings) {
    const ReadingLookup lookup = ReadingsOf(readings);
    const KanaSearch search = {
        .lookup = lookup,
        .starts_word =
            [readings](absl::string_view kana) {
              return std::any_of(
                  readings.begin(), readings.end(),
                  [&](const std::string& r) { return r.starts_with(kana); });
            },
        .kana_of_keys =
            [](absl::string_view keys) {
              return std::string(keys == "wo"   ? "を"
                                 : keys == "yo" ? "よ"
                                                : "");
            },
    };
    std::vector<std::string> raws, query, preedit;
    for (const std::vector<std::string>& c : chars) {
      raws.push_back(c[0]);
      query.push_back(c[1]);
      preedit.push_back(c.size() > 2 ? c[2] : c[1]);
    }
    return EnglishSpans(
        raws, query, preedit, words,
        [&](absl::string_view kana) { return ReadsAsJapanese(kana, lookup); },
        &search);
  }
};

TEST_F(KanaSearchTest, FindsAWordInsideJapanese) {
  const EnglishWords words("file\nsite\n");
  // filewohiraku: ふぃぇ is no Japanese; を and ひらく are.
  EXPECT_EQ(Spans({{"f", "ふ"},
                   {"i", "ぃ"},
                   {"le", "ぇ"},
                   {"wo", "を"},
                   {"hi", "ひ"},
                   {"ra", "ら"},
                   {"ku", "く"}},
                  words, {"ひらく"}),
            (Spans_{{0, 3, "file", ""}}));
  // atarasiisitewotukuru: all of it reads as Japanese (し and て too), so
  // site stays して.
  EXPECT_EQ(Spans({{"a", "あ"},
                   {"ta", "た"},
                   {"ra", "ら"},
                   {"si", "し"},
                   {"i", "い"},
                   {"si", "し"},
                   {"te", "て"},
                   {"wo", "を"},
                   {"tu", "つ"},
                   {"ku", "く"},
                   {"ru", "る"}},
                  words, {"あたらしい", "つくる"}),
            Spans_());
  // What is left must read as Japanese: べふぉ does not.
  EXPECT_EQ(Spans({{"f", "ふ"},
                   {"i", "ぃ"},
                   {"le", "ぇ"},
                   {"be", "べ"},
                   {"f", "ふ"},
                   {"o", "ぉ"}},
                  words, {}),
            Spans_());
}

TEST_F(KanaSearchTest, TakesTheFewestAndThenTheLongestWords) {
  const EnglishWords words("online\nline\n");
  // onlinede: online, not お + ん + line.
  EXPECT_EQ(
      Spans(
          {{"o", "お"}, {"n", "ん"}, {"li", "ぃ"}, {"ne", "ね"}, {"de", "で"}},
          words, {}),
      (Spans_{{0, 4, "online", ""}}));
}

TEST_F(KanaSearchTest, AWordMayEndInTheKanaAfterIt) {
  const EnglishWords words("event\ntwo\n");
  // eventwomiru: two made とぉ, split t|wo; wo is typed again as を.
  EXPECT_EQ(Spans({{"e", "え"},
                   {"v", "ゔ"},
                   {"e", "ぇ"},
                   {"n", "ん"},
                   {"t", "と"},
                   {"wo", "ぉ"},
                   {"mi", "み"},
                   {"ru", "る"}},
                  words, {"みる"}),
            (Spans_{{0, 6, "event", "wo"}}));
}

TEST_F(KanaSearchTest, AWordStillBeingTypedIsJapanese) {
  const EnglishWords words("sore\n");
  // sorehabe may go on to べんり: nothing yet.
  EXPECT_EQ(Spans({{"so", "そ"}, {"re", "れ"}, {"ha", "は"}, {"be", "べ"}},
                  words, {"それ", "べんり"}),
            Spans_());
}

TEST_F(KanaSearchTest, LeavesKanaBesideALooseLetterAlone) {
  const EnglishWords words("file\n");
  // filewohoz: z may still make a kana.
  EXPECT_EQ(Spans({{"f", "ふ"},
                   {"i", "ぃ"},
                   {"le", "ぇ"},
                   {"wo", "を"},
                   {"ho", "ほ"},
                   {"z", "z", "ｚ"}},
                  words, {}),
            Spans_());
}

TEST_F(KanaSearchTest, FindsAWordAfterOneKeptAsTyped) {
  const EnglishWords words("file\ndelete\ntwo\n");
  // file was kept at a pause; then wodeletesuru.
  EXPECT_EQ(Spans({{"f", "f"},
                   {"i", "i"},
                   {"l", "l"},
                   {"e", "e"},
                   {"wo", "を"},
                   {"de", "で"},
                   {"le", "ぇ"},
                   {"te", "て"},
                   {"su", "す"},
                   {"ru", "る"}},
                  words, {}),
            (Spans_{{5, 8, "delete", ""}}));
  // Letters do not run on unspaced: no two right after file.
  EXPECT_EQ(Spans({{"f", "f"},
                   {"i", "i"},
                   {"l", "l"},
                   {"e", "e"},
                   {"t", "と"},
                   {"wo", "ぉ"},
                   {"mi", "み"},
                   {"ru", "る"}},
                  words, {"みる"}),
            Spans_());
}

TEST_F(KanaSearchTest, FindsAWordBesideOneWithLooseLetters) {
  const EnglishWords words("file\nupdate\n");
  // konofilewoupdatesuru.
  EXPECT_EQ(Spans({{"ko", "こ"},
                   {"no", "の"},
                   {"f", "ふ"},
                   {"i", "ぃ"},
                   {"le", "ぇ"},
                   {"wo", "を"},
                   {"u", "う"},
                   {"p", "p", "ｐ"},
                   {"da", "だ"},
                   {"te", "て"},
                   {"su", "す"},
                   {"ru", "る"}},
                  words, {"この"}),
            (Spans_{{2, 5, "file", ""}, {6, 10, "update", ""}}));
}

TEST_F(KanaSearchTest, ARunThatIsAWordIsLookedAtAsAWhole) {
  const EnglishWords words("made\ncha\n");
  EXPECT_EQ(Spans({{"ma", "ま"}, {"de", "で"}}, words, {"まで"}), Spans_());
  // てぇ is a word of the dictionary, though of one mora.
  EXPECT_EQ(Spans({{"th", "て"}, {"e", "ぇ"}}, EnglishWords("the\n"), {"てぇ"}),
            Spans_());
}

TEST_F(KanaSearchTest, AKanaWithASmallYaYuYoIsAWord) {
  const EnglishWords words("six\nsave\n");
  // しょをえる (書を得る), typed si + xyo: not six + よ.
  EXPECT_EQ(Spans({{"si", "し"},
                   {"xyo", "ょ"},
                   {"wo", "を"},
                   {"e", "え"},
                   {"ru", "る"}},
                  words, {"しょ"}),
            Spans_());
  // ゔぇ is in the dictionary too, but is no word: さゔぇする keeps save.
  EXPECT_EQ(
      Spans(
          {{"sa", "さ"}, {"v", "ゔ"}, {"e", "ぇ"}, {"su", "す"}, {"ru", "る"}},
          words, {"ゔぇ"}),
      (Spans_{{0, 3, "save", ""}}));
}

TEST_F(KanaSearchTest, ALongVowelCountsInAWord) {
  const EnglishWords words("save\n");
  // きーをさゔぇする (キーをsaveする): きー is two morae, so what is left of
  // save reads as Japanese.
  EXPECT_EQ(Spans({{"ki", "き"},
                   {"-", "ー"},
                   {"wo", "を"},
                   {"sa", "さ"},
                   {"v", "ゔ"},
                   {"e", "ぇ"},
                   {"su", "す"},
                   {"ru", "る"}},
                  words, {"きー"}),
            (Spans_{{3, 6, "save", ""}}));
}

TEST(EnglishWordsDefaultTest, HasSCOWLsWords) {
  EXPECT_GT(EnglishWords::Default().size(), 80000);
  EXPECT_TRUE(EnglishWords::Default().Contains("keyboard"));
  EXPECT_TRUE(EnglishWords::Default().Contains("email"))
      << "newer than macOS's 1934 list";
}

TEST(EnglishWordsDefaultTest, HasThePresetWordsAsWordsAUserAdded) {
  const EnglishWords& words = EnglishWords::Default();
  EXPECT_TRUE(words.Contains("json")) << "not in SCOWL";
  EXPECT_TRUE(words.ContainsUser("json"));
  EXPECT_TRUE(words.ContainsUser("page")) << "ぱげ";
  EXPECT_TRUE(words.ContainsUser("time")) << "ちめ";
  EXPECT_FALSE(words.ContainsUser("keyboard")) << "SCOWL's";
  EXPECT_FALSE(words.ContainsUser("site")) << "して is typed for Japanese";
  EXPECT_FALSE(words.ContainsUser("made")) << "まで";
}

TEST(EnglishWordsDefaultTest, EveryPresetLineIsAWord) {
  const absl::StatusOr<std::string> text = FileUtil::GetContents(
      testing::GetSourceFileOrDie({"floatype", "english", "preset_words.txt"}));
  ASSERT_OK(text);
  const UserEnglishWords preset = ParseUserEnglishWords(*text);
  EXPECT_THAT(preset.left_out, ::testing::IsEmpty());
  EXPECT_EQ(preset.removed, "");
  const std::vector<std::string> lines =
      absl::StrSplit(preset.words, '\n', absl::SkipEmpty());
  EXPECT_GT(lines.size(), 100);
  for (const std::string& word : lines) {
    EXPECT_EQ(word, absl::AsciiStrToLower(word));
    EXPECT_TRUE(EnglishWords::Default().ContainsUser(word)) << word;
  }
  EXPECT_EQ(std::set<std::string>(lines.begin(), lines.end()).size(),
            lines.size())
      << "a word twice";
}

TEST(EnglishWordsRemovedTest, TakesWordsOutOfBothLists) {
  const EnglishWords words("keyboard\ntime\nemail\n", "time\nFigma\n",
                           "Time\nkeyboard\n");
  EXPECT_FALSE(words.Contains("time"));
  EXPECT_FALSE(words.ContainsUser("time"));
  EXPECT_FALSE(words.Contains("keyboard"));
  EXPECT_TRUE(words.Contains("email"));
  EXPECT_TRUE(words.ContainsUser("figma"));
  EXPECT_EQ(words.size(), 2);
}

TEST(UserEnglishWordsTest, LeavesOutWhatNoSpanTakes) {
  const UserEnglishWords user = ParseUserEnglishWords(
      "# mine\r\n"
      "Figma\r\n"
      "\n"
      "  json  \n"
      "go\n"
      "e-mail\n"
      "abcdefghijklmnopqrstuvwxyzabcdefg\n");
  EXPECT_EQ(user.words, "Figma\njson\n");
  EXPECT_EQ(user.left_out, (std::vector<std::string>{
                               "5 行目「go」", "6 行目「e-mail」",
                               "7 行目「abcdefghijklmnopqrstuvwxyzabcdefg」"}));
}

TEST(UserEnglishWordsTest, ALineStartingWithAHyphenTakesAWordOut) {
  const UserEnglishWords user = ParseUserEnglishWords(
      "figma\n"
      "-time\n"
      "  - Page  \n"
      "-go\n"
      "-\n");
  EXPECT_EQ(user.words, "figma\n");
  EXPECT_EQ(user.removed, "time\nPage\n");
  EXPECT_EQ(user.left_out,
            (std::vector<std::string>{"4 行目「-go」", "5 行目「-」"}));
}

class CurrentEnglishWordsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Clock::SetClockForUnitTest(&clock_);
    SetUserFilesDir(dir_.path(), /*report_errors=*/false);
  }
  void TearDown() override {
    SetUserFilesDir("", false);
    Clock::SetClockForUnitTest(nullptr);
  }

  void Write(const std::string& text) {
    ASSERT_OK(FileUtil::SetContents(
        FileUtil::JoinPath(dir_.path(), kEnglishWordsFile), text));
  }

  ClockMock clock_{absl::FromUnixSeconds(1000)};
  TempDirectory dir_ = TempDirectory::Default().CreateTempDirectory().value();
};

TEST_F(CurrentEnglishWordsTest, WithoutAFileIsTheBuiltInList) {
  EXPECT_EQ(CurrentEnglishWords().get(), &EnglishWords::Default());
}

TEST_F(CurrentEnglishWordsTest, AddsTheUsersWordsAndReadsEdits) {
  Write("floatype\n");
  std::shared_ptr<const EnglishWords> words = CurrentEnglishWords();
  EXPECT_TRUE(words->Contains("Floatype"));
  EXPECT_TRUE(words->Contains("keyboard"));
  EXPECT_TRUE(words->ContainsUser("page")) << "the preset words stay";
  EXPECT_FALSE(words->Contains("kanakanji"));

  Write("kanakanji\nmore\n");
  EXPECT_FALSE(CurrentEnglishWords()->Contains("kanakanji"))
      << "looks once a second";
  clock_.Advance(absl::Seconds(1));
  EXPECT_TRUE(CurrentEnglishWords()->Contains("kanakanji"));
  EXPECT_FALSE(CurrentEnglishWords()->Contains("floatype"));

  ASSERT_OK(
      FileUtil::Unlink(FileUtil::JoinPath(dir_.path(), kEnglishWordsFile)));
  clock_.Advance(absl::Seconds(1));
  EXPECT_EQ(CurrentEnglishWords().get(), &EnglishWords::Default());
}

TEST_F(CurrentEnglishWordsTest, TakesOutTheWordsTheFileSays) {
  Write("-page\n-keyboard\nsite\n");
  const std::shared_ptr<const EnglishWords> words = CurrentEnglishWords();
  EXPECT_FALSE(words->Contains("page")) << "a preset word";
  EXPECT_FALSE(words->Contains("keyboard")) << "SCOWL's";
  EXPECT_TRUE(words->ContainsUser("time")) << "the other preset words stay";
  EXPECT_TRUE(words->ContainsUser("site"));
}

}  // namespace
}  // namespace mozc::floatype
