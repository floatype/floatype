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
  // With zenz_stop_when_idle, the scorer ends after this long without a
  // request; it starts again in about 50ms on the next key.
  static constexpr int kIdleExitSeconds = 60;
  // How long the client waits before calling back.
  static constexpr absl::Duration kPollInterval = absl::Milliseconds(10);
  // Longer readings are left to Mozc. The scorer's context (512 tokens) holds
  // 40 characters of context, the reading and an answer as long as the
  // reading for readings up to about 230 characters.
  static constexpr size_t kMaxReadingChars = 200;
  // How often one "not this" asks the scorer, while its answers would not
  // change what is shown (see Request()). As many as the scorer's forks
  // (kMaxDeviations in scorer_main.cc); each ask decodes at least one more.
  static constexpr int kMaxAsks = 8;

  // `client` may be null, which turns zenz off.
  explicit ZenzCorrector(ScorerClient* client) : client_(client) {}

  // Sends the conversion's reading and `left_context` to the scorer. Returns
  // true when an answer may follow, so the client should call back.
  //
  // With `not_this`, the user turned down the conversion on screen. zenz is
  // asked for an answer other than it and than every conversion turned down
  // or answered without showing for the same reading and context since the
  // last request without `not_this`. While its answers would not change what
  // is shown, Poll() asks again, up to kMaxAsks times within the timeout.
  bool Request(const Segments& segments, absl::string_view left_context,
               bool not_this = false);

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
  // Whether answers the user stopped (ZenzMemory::Stopped()) are kept off
  // screen: the zenz_learning setting.
  void set_learning(bool learning) { learning_ = learning; }
  // The settings zenz_stop_when_idle and zenz_low_memory, applied from the
  // scorer's next start. Call it before Warm() and Request().
  void Configure(bool stop_when_idle, bool low_memory);
  // Starts the scorer ahead of a request, while the user types.
  void Warm();
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
    ::floatype::scorer::Request request;
    // When the first request went; asking again keeps it.
    absl::Time sent;
    // The conversion the answer is for; see Fingerprint() in the .cc file.
    std::string fingerprint;
    bool not_this = false;
    int asks = 1;
  };

  // kApply when `response` changes what is shown, otherwise kDone.
  PollResult Judge(const Segments& segments,
                   const ::floatype::scorer::Response& response) const;
  void Reject(absl::string_view conversion);

  ScorerClient* const client_;
  absl::Duration timeout_ = kTimeout;
  bool learning_ = false;
  std::optional<Pending> pending_;
  // What zenz may not answer for "<reading>\t<left context>" in
  // rejected_for_, kept for Request() with `not_this`.
  std::string rejected_for_;
  std::vector<std::string> rejected_;
};

// Live conversion and the indicator run only where the converter process
// turned them on at startup, so Mozc's own tests, whose tools answer callbacks
// at once and compare whole outputs, behave as before.
void EnableSessionFeatures(bool enabled);
bool SessionFeaturesEnabled();

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_ZENZ_CORRECTOR_H_
