// Runs floatype_scorer as a child process and talks to it without blocking.

#ifndef MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_
#define MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "floatype/scorer/protocol.h"

namespace mozc::floatype {

// The scorer is started on the first request. When it dies, it is started
// again on a later request, at most once per kRestartInterval, so a scorer
// that cannot run does not cost a process launch on every conversion.
class ScorerClient {
 public:
  static constexpr absl::Duration kRestartInterval = absl::Seconds(10);

  // `command` is the scorer's argv, starting with the executable's path.
  explicit ScorerClient(std::vector<std::string> command);
  ScorerClient(const ScorerClient&) = delete;
  ScorerClient& operator=(const ScorerClient&) = delete;
  ~ScorerClient();

  // Sends a request. Returns false when the scorer cannot take it; then no
  // response will come for it.
  bool Send(const ::floatype::scorer::Request& request);

  // Reads what has arrived and returns the latest response so far, if any.
  // The response stays until a newer one arrives, so that every caller sharing
  // the scorer can look for its own request id.
  std::optional<::floatype::scorer::Response> Poll();

 private:
  bool Start();
  void Stop();

  const std::vector<std::string> command_;
  int fd_ = -1;
  pid_t pid_ = -1;
  std::string buffer_;
  std::optional<::floatype::scorer::Response> latest_;
  absl::Time last_start_ = absl::InfinitePast();
};

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_
