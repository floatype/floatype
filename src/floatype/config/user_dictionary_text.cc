#include "floatype/config/user_dictionary_text.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "dictionary/user_dictionary_util.h"
#include "dictionary/user_pos.h"
#include "protocol/user_dictionary_storage.pb.h"

namespace mozc::floatype {
namespace {

using user_dictionary::UserDictionary;
using user_dictionary::UserDictionaryStorage;

// A reading may start with #, which would make its line a comment; it is
// written in full width, which the converter reads as #.
constexpr absl::string_view kHash = "#";
constexpr absl::string_view kFullWidthHash = "＃";

std::vector<absl::string_view> Lines(absl::string_view text) {
  absl::ConsumePrefix(&text, "\xEF\xBB\xBF");  // A BOM an editor may add.
  std::vector<absl::string_view> lines = absl::StrSplit(text, '\n');
  if (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  for (absl::string_view& line : lines) {
    absl::ConsumeSuffix(&line, "\r");
  }
  return lines;
}

bool IsComment(absl::string_view line) {
  return line.starts_with('#') || absl::StripAsciiWhitespace(line).empty();
}

// The dictionary name of a "[name]" line.
std::optional<absl::string_view> Header(absl::string_view line) {
  if (line.size() < 2 || !line.starts_with('[') || !line.ends_with(']') ||
      absl::StrContains(line, '\t')) {
    return std::nullopt;
  }
  return line.substr(1, line.size() - 2);
}

std::string Describe(const absl::Status& status) {
  using enum user_dictionary::ExtendedErrorCode;
  switch (status.raw_code()) {
    case DICTIONARY_NAME_EMPTY:
      return "辞書名が空です";
    case DICTIONARY_NAME_TOO_LONG:
      return "辞書名が長すぎます（300 バイトまで）";
    case READING_EMPTY:
      return "よみが空です";
    case READING_TOO_LONG:
      return "よみが長すぎます（300 バイトまで）";
    case WORD_EMPTY:
      return "単語が空です";
    case WORD_TOO_LONG:
      return "単語が長すぎます（300 バイトまで）";
    case COMMENT_TOO_LONG:
      return "コメントが長すぎます（300 バイトまで）";
    default:
      return "使えない文字（改行、UTF-8 でない文字）があります";
  }
}

absl::StatusOr<UserDictionary::Entry> ParseEntry(absl::string_view line) {
  const std::vector<absl::string_view> fields = absl::StrSplit(line, '\t');
  if (fields.size() < 3 || fields.size() > 4) {
    return absl::InvalidArgumentError(
        "よみ、単語、品詞、コメント（なくてもよい）をタブで区切って書きます");
  }
  const std::pair<absl::string_view, absl::string_view> pos = absl::StrSplit(
      absl::StripAsciiWhitespace(fields[2]), absl::MaxSplits(':', 1));
  // Checked before it is set: a debug build stops on a value that is no type.
  const UserDictionary::PosType type =
      dictionary::UserPos::ToPosType(pos.first);
  if (!UserDictionary::PosType_IsValid(type)) {
    return absl::InvalidArgumentError(
        absl::StrCat("品詞「", pos.first, "」はありません"));
  }
  UserDictionary::Entry entry;
  entry.set_pos(type);
  absl::string_view key = fields[0];
  *entry.mutable_key() = absl::ConsumePrefix(&key, kFullWidthHash)
                             ? absl::StrCat(kHash, key)
                             : std::string(key);
  entry.set_value(fields[1]);
  if (fields.size() == 4) {
    entry.set_comment(fields[3]);
  }
  if (!pos.second.empty()) {
    entry.set_locale(pos.second);  // Mozc's importer's "名詞:en".
  }
  if (absl::Status status = user_dictionary::ValidateEntry(entry);
      !status.ok()) {
    return absl::InvalidArgumentError(Describe(status));
  }
  return entry;
}

std::string FormatEntry(const UserDictionary::Entry& entry) {
  absl::string_view key = entry.key();
  std::string line = absl::StrCat(
      absl::ConsumePrefix(&key, kHash) ? kFullWidthHash : "", key, "\t",
      entry.value(), "\t", dictionary::UserPos::GetStringPosType(entry.pos()));
  if (!entry.locale().empty()) {
    absl::StrAppend(&line, ":", entry.locale());
  }
  if (!entry.comment().empty()) {
    absl::StrAppend(&line, "\t", entry.comment());
  }
  return line;
}

absl::Status ErrorAt(size_t line, absl::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat(line, " 行目: ", message));
}

}  // namespace

absl::StatusOr<UserDictionaryStorage> ParseUserDictionary(
    absl::string_view text, size_t max_dictionaries, size_t max_entries) {
  UserDictionaryStorage storage;
  absl::flat_hash_set<std::string> names;
  UserDictionary* dictionary = nullptr;
  const std::vector<absl::string_view> lines = Lines(text);
  for (size_t i = 0; i < lines.size(); ++i) {
    const absl::string_view line = lines[i];
    if (IsComment(line)) {
      continue;
    }
    if (const std::optional<absl::string_view> name = Header(line)) {
      if (absl::Status status = user_dictionary::ValidateDictionaryName(*name);
          !status.ok()) {
        return ErrorAt(i + 1, Describe(status));
      }
      if (!names.emplace(*name).second) {
        return ErrorAt(i + 1,
                       absl::StrCat("辞書「", *name, "」が 2 つあります"));
      }
      if (storage.dictionaries_size() >= max_dictionaries) {
        return ErrorAt(
            i + 1, absl::StrCat("辞書は ", max_dictionaries, " 個までです"));
      }
      dictionary = storage.add_dictionaries();
      dictionary->set_name(*name);
      continue;
    }
    if (!absl::StrContains(line, '\t')) {
      return ErrorAt(
          i + 1,
          "[辞書名] の行か、よみ、単語、品詞をタブで区切った行を書きます");
    }
    if (dictionary == nullptr) {
      return ErrorAt(i + 1, "単語の前に [辞書名] の行が要ります");
    }
    absl::StatusOr<UserDictionary::Entry> entry = ParseEntry(line);
    if (!entry.ok()) {
      return ErrorAt(i + 1, entry.status().message());
    }
    if (dictionary->entries_size() >= max_entries) {
      return ErrorAt(i + 1, absl::StrCat("1 つの辞書の単語は ", max_entries,
                                         " 個までです"));
    }
    *dictionary->add_entries() = *std::move(entry);
  }
  return storage;
}

std::string FormatUserDictionary(const UserDictionaryStorage& storage,
                                 absl::string_view old_text) {
  // The comment lines of `old_text` at its head, at its end, and before each
  // line, keyed as the line is written.
  std::vector<absl::string_view> head, block;
  absl::flat_hash_map<std::string, std::vector<absl::string_view>> before;
  bool started = false;
  std::string name;
  for (const absl::string_view line : Lines(old_text)) {
    if (IsComment(line)) {
      block.push_back(line);
      continue;
    }
    std::string key;
    if (const std::optional<absl::string_view> header = Header(line)) {
      name = std::string(*header);
      key = std::string(line);
    } else {
      absl::StatusOr<UserDictionary::Entry> entry = ParseEntry(line);
      key = absl::StrCat(name, "\n",
                         entry.ok() ? FormatEntry(*entry) : std::string(line));
    }
    std::vector<absl::string_view>& comments = started ? before[key] : head;
    comments.insert(comments.end(), block.begin(), block.end());
    block.clear();
    started = true;
  }
  if (!started) {
    head.swap(block);  // Only comments: they all stay at the head.
  }

  std::string text;
  bool blank = false;  // Whether the last line written is blank.
  const auto add = [&](absl::string_view line) {
    absl::StrAppend(&text, line, "\n");
    blank = absl::StripAsciiWhitespace(line).empty();
  };
  const auto add_comments = [&](const std::string& key) {
    const auto it = before.find(key);
    if (it == before.end()) {
      return false;
    }
    for (const absl::string_view line : it->second) {
      add(line);
    }
    before.erase(it);
    return true;
  };
  for (const absl::string_view line : head) {
    add(line);
  }
  for (const UserDictionary& dictionary : storage.dictionaries()) {
    const std::string header = absl::StrCat("[", dictionary.name(), "]");
    // A blank line between dictionaries, unless comments are there.
    if (!add_comments(header) && &dictionary != &storage.dictionaries(0) &&
        !blank) {
      add("");
    }
    add(header);
    for (const UserDictionary::Entry& entry : dictionary.entries()) {
      if (!user_dictionary::ValidateEntry(entry).ok()) {
        continue;
      }
      const std::string line = FormatEntry(entry);
      add_comments(absl::StrCat(dictionary.name(), "\n", line));
      add(line);
    }
  }
  for (const absl::string_view line : block) {
    add(line);
  }
  return text;
}

}  // namespace mozc::floatype
