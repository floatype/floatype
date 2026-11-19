#include "floatype/config/user_dictionary_text.h"

#include <string>

#include "absl/status/statusor.h"
#include "protocol/user_dictionary_storage.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using ::testing::HasSubstr;
using user_dictionary::UserDictionary;
using user_dictionary::UserDictionaryStorage;

UserDictionaryStorage Parse(const std::string& text) {
  absl::StatusOr<UserDictionaryStorage> storage =
      ParseUserDictionary(text, 100, 1000000);
  EXPECT_OK(storage);
  return storage.value_or(UserDictionaryStorage());
}

void ExpectErrorAt(const std::string& text, const std::string& where,
                   size_t max_dictionaries = 100,
                   size_t max_entries = 1000000) {
  absl::StatusOr<UserDictionaryStorage> storage =
      ParseUserDictionary(text, max_dictionaries, max_entries);
  ASSERT_FALSE(storage.ok()) << text;
  EXPECT_THAT(storage.status().message(), HasSubstr(where)) << storage.status();
}

TEST(UserDictionaryTextTest, ReadsTheDictionaryToolsExport) {
  const UserDictionaryStorage storage = Parse(
      "# mine\r\n"
      "[辞書 1]\r\n"
      "ふろーと\tfloatype\t固有名詞\tthe IME\r\n"
      "\n"
      "[辞書 2]\n"
      "あ\tア\t名詞:en\n"
      "  \n"
      "[空]\n");
  ASSERT_EQ(storage.dictionaries_size(), 3);
  EXPECT_EQ(storage.dictionaries(0).name(), "辞書 1");
  const UserDictionary::Entry& entry = storage.dictionaries(0).entries(0);
  EXPECT_EQ(entry.key(), "ふろーと");
  EXPECT_EQ(entry.value(), "floatype");
  EXPECT_EQ(entry.pos(), UserDictionary::PROPER_NOUN);
  EXPECT_EQ(entry.comment(), "the IME");
  EXPECT_EQ(storage.dictionaries(1).entries(0).locale(), "en");
  EXPECT_EQ(storage.dictionaries(1).entries(0).comment(), "");
  EXPECT_EQ(storage.dictionaries(2).entries_size(), 0);
}

TEST(UserDictionaryTextTest, RoundTrips) {
  UserDictionaryStorage storage;
  UserDictionary* dictionary = storage.add_dictionaries();
  dictionary->set_name("[a] #b");  // Any name Mozc takes.
  UserDictionary::Entry* entry = dictionary->add_entries();
  entry->set_key("#tag");  // Starts like a comment.
  entry->set_value("ハッシュ");
  entry->set_pos(UserDictionary::SUGGESTION_ONLY);
  entry->set_locale("ja");
  entry = dictionary->add_entries();
  entry->set_key("てすと");
  entry->set_value("テスト");
  entry->set_pos(UserDictionary::NO_POS);
  entry->set_comment("memo");
  storage.add_dictionaries()->set_name("two");

  const std::string text = FormatUserDictionary(storage, "");
  EXPECT_EQ(text,
            "[[a] #b]\n"
            "＃tag\tハッシュ\tサジェストのみ:ja\n"
            "てすと\tテスト\t品詞なし\tmemo\n"
            "\n"
            "[two]\n");
  EXPECT_EQ(Parse(text).SerializeAsString(), storage.SerializeAsString());
}

TEST(UserDictionaryTextTest, LeavesOutWordsMozcDoesNotUse) {
  UserDictionaryStorage storage;
  UserDictionary* dictionary = storage.add_dictionaries();
  dictionary->set_name("d");
  dictionary->add_entries()->set_comment("an empty row of the tool");
  EXPECT_EQ(FormatUserDictionary(storage, ""), "[d]\n");
}

TEST(UserDictionaryTextTest, KeepsComments) {
  constexpr char kOld[] =
      "# my words\n"
      "\n"
      "[d]\n"
      "# the IME\n"
      "ふろーと\tfloatype\t固有名詞\n"
      "# gone\n"
      "けす\t消す\t名詞\n"
      "あ\tア\t名詞\n"
      "\n"
      "# second\n"
      "[e]\n"
      "# end\n";
  UserDictionaryStorage storage = Parse(kOld);
  // The tool deletes one word and adds another.
  storage.mutable_dictionaries(0)->mutable_entries()->DeleteSubrange(1, 1);
  UserDictionary::Entry* entry = storage.mutable_dictionaries(0)->add_entries();
  entry->set_key("い");
  entry->set_value("イ");
  entry->set_pos(UserDictionary::NOUN);
  EXPECT_EQ(FormatUserDictionary(storage, kOld),
            "# my words\n"
            "\n"
            "[d]\n"
            "# the IME\n"
            "ふろーと\tfloatype\t固有名詞\n"
            "あ\tア\t名詞\n"
            "い\tイ\t名詞\n"
            "\n"
            "# second\n"
            "[e]\n"
            "# end\n");
}

TEST(UserDictionaryTextTest, CommentsOfAFileWithoutWordsStayAtTheHead) {
  UserDictionaryStorage storage;
  storage.add_dictionaries()->set_name("d");
  EXPECT_EQ(FormatUserDictionary(storage, "# my words\n"), "# my words\n[d]\n");
}

TEST(UserDictionaryTextTest, ErrorsGiveTheLine) {
  ExpectErrorAt("[d]\nあ\tア\tめいし\n", "2 行目: 品詞「めいし」はありません");
  ExpectErrorAt("あ\tア\t名詞\n", "1 行目: 単語の前に");
  ExpectErrorAt("[d]\nあ ア 名詞\n", "2 行目");
  ExpectErrorAt("[d]\nあ\tア\n", "2 行目");
  ExpectErrorAt("[d]\nあ\tア\t名詞\tc\textra\n", "2 行目");
  ExpectErrorAt("[d]\n\tア\t名詞\n", "2 行目: よみが空です");
  ExpectErrorAt("[d]\nあ\t\t名詞\n", "2 行目: 単語が空です");
  ExpectErrorAt("[d]\n" + std::string(301, 'a') + "\tア\t名詞\n",
                "2 行目: よみが長すぎます");
  ExpectErrorAt("[]\n", "1 行目: 辞書名が空です");
  ExpectErrorAt("[d]\n\n[d]\n", "3 行目: 辞書「d」が 2 つあります");
  ExpectErrorAt("[a]\n[b]\n", "2 行目: 辞書は 1 個まで", 1);
  ExpectErrorAt("[a]\nあ\tア\t名詞\nい\tイ\t名詞\n",
                "3 行目: 1 つの辞書の単語は 1 個まで", 100, 1);
}

}  // namespace
}  // namespace mozc::floatype
