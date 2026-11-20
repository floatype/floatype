// floatype: telling an English word typed in a kana mode (spec section 2,
// mixed Japanese and English input).

#ifndef MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
#define MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace mozc::floatype {

// A word list, compared without case.
class EnglishWords {
 public:
  // `text` has one word a line; so does `user_text`, the words the user
  // added, which are in the list too, and `removed_text`, the words the user
  // took out, which are in neither.
  explicit EnglishWords(absl::string_view text,
                        absl::string_view user_text = "",
                        absl::string_view removed_text = "");
  bool Contains(absl::string_view word) const;
  // Whether the user added `word`: English even where its keys also spell a
  // Japanese word (time and ちめ).
  bool ContainsUser(absl::string_view word) const;
  size_t size() const { return words_.size(); }

  // SCOWL's size-60 American list (english_words.txt, made by
  // floatype/tools/make_english_words.py) and, as words a user added, the
  // preset words (preset_words.txt: page, time, json), built into the binary.
  static const EnglishWords& Default();

 private:
  std::vector<std::string> words_;       // Lower case, sorted.
  std::vector<std::string> user_words_;  // Lower case, sorted.
};

// The words a user's english_words.txt adds, one a line, the words it takes
// out of the lists ("-time", a line starting with -), and the lines left out
// ("N 行目「go」"): those not 3 to 32 ASCII letters, which no span takes.
// Lines starting with # and blank lines are comments.
struct UserEnglishWords {
  std::string words;    // One a line.
  std::string removed;  // One a line.
  std::vector<std::string> left_out;
};
UserEnglishWords ParseUserEnglishWords(absl::string_view text);

// The words that count as English: Default(), and while the user has an
// english_words.txt next to config.toml, with the words it adds and without
// those it takes out, read again a second after it changes. Lines left out
// are reported.
std::shared_ptr<const EnglishWords> CurrentEnglishWords();

// Whether the keys `raw` typed into the composition queried as `preedit` (with
// a trailing n already ん, so ten stays てん) were an
// English word: the romaji left letters unconverted (まcぼおk for macbook,
// unlike へっぉ for hello, which stays Japanese), and `raw`, at least three
// letters, is in `words`.
bool LooksEnglish(absl::string_view raw, absl::string_view preedit,
                  const EnglishWords& words);

// Calls `reading` with the reading of each dictionary word whose reading
// starts `key` from its first character (EngineConverter's LookupWords()).
using ReadingLookup = std::function<void(
    absl::string_view key,
    absl::FunctionRef<void(absl::string_view reading)> reading)>;

// Whether `kana` reads as Japanese: dictionary words of two kana or more and
// particles of one kana (は, が, ね) cover it from end to end. わね and ので
// do; べふぉれ (before) and おぺん (open) do not. A single kana is nearly
// always some word, so it counts only as a particle.
bool ReadsAsJapanese(absl::string_view kana, const ReadingLookup& lookup);

// What EnglishSpans() asks to find words typed wholly into kana inside
// Japanese (ふぃぇをほぞんした for fileを保存した).
struct KanaSearch {
  // The dictionary's readings, as for ReadsAsJapanese().
  ReadingLookup lookup;
  // Whether the reading of some dictionary word starts with `kana`
  // (EngineConverter's StartsWord()): a word still being typed.
  std::function<bool(absl::string_view kana)> starts_word;
  // The kana that `keys` type on their own (を for wo), or "" where a letter
  // stays.
  std::function<std::string(absl::string_view keys)> kana_of_keys;
  // How costly `text`, kana with or without words in letters, is to convert
  // (EngineConverter's ConversionCost()); negative where it cannot tell.
  // Without it, no word is told by its cost.
  std::function<int(absl::string_view text)> cost;
  // The composition follows Japanese text, which its first kana may go on
  // with: no word at its start is told by its cost.
  bool after_japanese = false;
};

// An English word inside a composition: characters [begin, end), typed as
// `word` and then `retyped`, keys the romaji joined to the kana after the
// word (test + する shows てsつる; email + を shows えまいlwお). The
// characters become the word's letters, and `retyped` is typed again.
struct EnglishSpan {
  size_t begin;
  size_t end;
  std::string word;
  std::string retyped;
  // Told by the cost of converting (KanaSearch's cost): what is typed after
  // it may tell otherwise, so the word is looked at again then.
  bool by_cost = false;
  bool operator==(const EnglishSpan&) const = default;
};

// The English words inside a composition whose i-th character was typed as
// `raws[i]`, reads `query[i]` (as in LooksEnglish) and shows `preedit[i]`. A
// word, at least three letters and in `words`, must hold a letter the romaji
// left over, and leave none after it within its span; longer words win, so
// "has" does not take the s of "slack". A span already shown as typed is left
// out. Keys retyped start with a consonant, and are not letters still loose at
// the end, which may yet make a longer word. With `search`, a word beside
// which the kana read as Japanese comes before a longer one beside which they
// do not (きょうはてsとぉ is 今日は + test + を, not きょう + hates + とぉ),
// a word that ends where a kana begins comes before one that takes that
// kana's first keys, and the s of し (shi) after a t, which the romaji left a
// letter, is typed again (てstsひます is test + します, not tests + ひます).
//
// With `reads_as_japanese`, a run of kana with no letter beside it whose keys
// are a word is a span too when the user added the word, or when the kana do
// not read as Japanese (べふぉれ for before, between other characters or the
// ends; not まで for made). Without it, kana stay kana.
//
// With `search`, words inside a stretch of kana are spans too, where the
// stretch does not read as Japanese: one-kana particles, endings and stems
// (は, た, い) and dictionary words of two morae or more, or of a kana with
// ゃ, ゅ or ょ, do not cover it, nor cover it up to a word still being typed
// at the end of the composition (それはべ may go on to べんり). The spans are
// then the fewest words, and among those the longest, between which every
// stretch does read as Japanese: ふぃぇをほぞんした keeps file,
// あたらしいしてをつくる (site) stays kana. A word may end in the kana after
// it (えゔぇんとぉ is event + wo), whose other keys are typed again. No word
// touches a letter or another word, and a stretch beside a loose letter is
// left alone.
//
// With `search`'s cost, a word inside a stretch that does read as Japanese
// piece by piece is a span too where converting the stretch with the word in
// letters costs enough less than converting its kana: しぜがおおきすぎる
// (し + ぜ + が...) is sizeが大きすぎる, and ちめをきろくする, with a word the
// user added, is timeを記録する. まけをみとめる stays 負けを認める, and
// ここでまつ holds no code. How much less is asked is said at the constants in
// the source, with the measurements behind them.
std::vector<EnglishSpan> EnglishSpans(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::function<bool(absl::string_view kana)>& reads_as_japanese =
        nullptr,
    const KanaSearch* search = nullptr);

// A composition that is one English word from end to end, as a span: its
// keys are letters only, with . , ! ? ; or : after them at most.
//   - In a run of English (`run`: a word and a space came just before), a
//     word of `words` or a short word of prose (a, is, to), whatever kana its
//     keys type (you is よう, are is あれ), and an option of a command
//     (-m, --help).
//   - At the Space key (`space`), such a word with a letter the romaji left
//     over (of is おf), and keys that typed no kana at all (ls, npm), which
//     can be nothing else.
// Nothing where the word already shows as typed.
std::optional<EnglishSpan> WholeEnglishWord(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    bool run, bool space);

// A composition that is English words typed with no space between them
// (timeismoney, goodmorning), as a span: its keys are letters only and make
// up two words or more of `words`, or of two letters where a letter stays
// that no kana takes (is, it, of). Only where the words in `spans` leave
// something that is not Japanese: letters the romaji left over, or kana
// that `reads_as_japanese` does not read or that are the keys of a word the
// user added (frontぱげ). あとでtest and pushして stay as they are, though
// at + ode + test and push + site are words.
std::optional<EnglishSpan> WordsRunTogether(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::vector<EnglishSpan>& spans,
    const std::function<bool(absl::string_view kana)>& reads_as_japanese);

// The pieces of a name joined by . _ / @ : = or - that are English too,
// beside one that is (`spans`, and letters kept as typed; digits alone tell
// nothing):
//   - a word of `words` whose keys typed kana that read as Japanese
//     (node_modules, src/main; not a word beside a period, それ。test and
//     test。それ, which may be two sentences, nor before -, えらーlog, where
//     ー is the long vowel of エラー);
//   - the ending of a file or a host after a period, where a letter stays,
//     and with it a word before the period (main.cc, example.com);
//   - what comes before @ (taro@example.com);
//   - a piece with a letter no kana takes, where more follows (usr/local);
//   - single letters, one of them no kana and not the last (a.b.c).
std::vector<EnglishSpan> JoinedNames(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::vector<EnglishSpan>& spans);

// The symbols and digits to type again as they were typed, each a span of one
// character, in a composition that holds no kana: English words (`spans`, and
// letters kept as typed) with symbols between them, which name a file, a
// command or an address (config.toml, @src/app.ts, --help, localhost:3000).
// Symbols that join such names are typed again where something follows them;
// those at the end may close a Japanese sentence (test。) and stay, unless
// `prose` says English is being written with spaces, where . , ! ? ; and :
// are typed again too (hello, world.). Brackets and quotes always stay
// (「file」).
std::vector<EnglishSpan> SymbolsAsTyped(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit,
    const std::vector<EnglishSpan>& spans, bool prose);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
