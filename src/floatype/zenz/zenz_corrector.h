// Corrects a Mozc conversion once with zenz's answer.

#ifndef MOZC_FLOATYPE_ZENZ_ZENZ_CORRECTOR_H_
#define MOZC_FLOATYPE_ZENZ_ZENZ_CORRECTOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "converter/segments.h"
#include "floatype/zenz/scorer_client.h"

namespace mozc::floatype {

// Returns, for each conversion segment, the index of the candidate to show so
// that the segments read `text`, preferring higher-ranked candidates. Width is
// ignored in the comparison, so Mozc's choice of full or half width stays. A
// segment whose top candidate the user's history promoted keeps it. Returns
// nullopt when `text` cannot be written with Mozc's candidates, which also
// rejects any text whose reading differs from the conversion's.
std::optional<std::vector<int>> MapToCandidates(const Segments& segments,
                                                absl::string_view text);
// The conversion's reading: its segment keys in hiragana.
std::string ConversionReading(const Segments& segments);

// The session calls Request() right after Mozc converts, Poll() whenever the
// client calls back, and Cancel() on any user action.
class ZenzCorrector {
 public:
  // Answers that come later are dropped, unless set_timeout() says otherwise
  // (the zenz_timeout_millisec setting).
  static constexpr absl::Duration kTimeout = absl::Milliseconds(300);
  // How long the client waits before calling back.
  static constexpr absl::Duration kPollInterval = absl::Milliseconds(10);
  // Longer readings are left to Mozc. The scorer's context (512 tokens) holds
  // 40 characters of context, the reading and an answer as long as the
  // reading for readings up to about 230 characters.
  static constexpr size_t kMaxReadingChars = 200;

  // `client` may be null, which turns zenz off.
  explicit ZenzCorrector(ScorerClient* client) : client_(client) {}

  // Sends the conversion's reading and `left_context` to the scorer. Returns
  // true when an answer may follow, so the client should call back.
  bool Request(const Segments& segments, absl::string_view left_context);

  struct PollResult {
    enum Kind {
      kWait,   // No answer yet; call back again.
      kApply,  // Show `candidate_indices`, one per conversion segment.
      kDone,   // Keep Mozc's conversion.
    };
    Kind kind = kDone;
    std::vector<int> candidate_indices;
    // With kApply: the conversion's reading in hiragana and zenz's text.
    std::string reading;
    std::string text;
  };
  PollResult Poll(const Segments& segments);

  void Cancel() { pending_.reset(); }
  // Whether a scorer is there to ask.
  bool available() const { return client_ != nullptr; }
  void set_timeout(absl::Duration timeout) { timeout_ = timeout; }
  // True while an answer is awaited.
  bool pending() const { return pending_.has_value(); }

  // The converter process's client for the bundled scorer and model. It is
  // null until EnableDefaultClient() is called, so tests never start a scorer.
  static ScorerClient* DefaultClient();
  // Called by the converter process at startup.
  static void EnableDefaultClient();
  static void SetDefaultClientForTesting(ScorerClient* client);

 private:
  struct Pending {
    uint64_t id = 0;
    absl::Time sent;
    // The conversion the answer is for; see Fingerprint() in the .cc file.
    std::string fingerprint;
  };

  ScorerClient* const client_;
  absl::Duration timeout_ = kTimeout;
  std::optional<Pending> pending_;
};

// Live conversion and the indicator run only where the converter process
// turned them on at startup, so Mozc's own tests, whose tools answer callbacks
// at once and compare whole outputs, behave as before.
void EnableSessionFeatures(bool enabled);
bool SessionFeaturesEnabled();

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_ZENZ_CORRECTOR_H_
